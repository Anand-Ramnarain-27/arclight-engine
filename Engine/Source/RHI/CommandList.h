#pragma once

#include "RHI/RHITypes.h"

#include <cstddef>
#include <span>
#include <string_view>
#include <type_traits>
#include <vector>

namespace arc::rhi
{
    class Buffer;
    class Pipeline;
    struct PipelineDesc;
    class Sampler;
    class Texture;
    class UploadRing;

    struct ColorAttachment
    {
        Texture* texture = nullptr;
        LoadOp load = LoadOp::Clear;
        StoreOp store = StoreOp::Store;
        ClearColor clearColor{};
    };

    struct DepthAttachment
    {
        /// Null for a pass without depth. Must have DepthStencil usage and the same size as the
        /// colour target.
        Texture* texture = nullptr;
        LoadOp load = LoadOp::Clear;
        StoreOp store = StoreOp::Store;
        /// 1 is the far plane with a standard projection, 0 with a reversed-Z one.
        float clearDepth = 1.0f;
    };

    struct RenderPassDesc
    {
        ColorAttachment color;
        DepthAttachment depth{};

        /// Shown as the pass's name in GPU debuggers. May be empty.
        std::string_view debugName{};
    };

    /// Records GPU work for one frame. Obtained from Device::beginFrame() and valid until
    /// Device::endFrame().
    ///
    /// Draw commands may only be recorded between beginRenderPass() and endRenderPass(); copies
    /// and barriers only outside a pass. Passes do not nest. On Metal a pass is a render command
    /// encoder, on D3D12 it sets the render targets and performs the load action.
    ///
    /// beginRenderPass() moves the colour target to the RenderTarget state, and the depth target to
    /// DepthWrite, itself. Every pipeline set in a pass must have been created for the pass's colour
    /// and depth formats.
    class CommandList
    {
    public:
        virtual ~CommandList() = default;

        CommandList(const CommandList&) = delete;
        CommandList& operator=(const CommandList&) = delete;

        virtual void beginRenderPass(const RenderPassDesc& desc) = 0;
        virtual void endRenderPass() = 0;

        /// The draw state below is only valid inside a render pass and is reset by each new pass.
        virtual void setPipeline(Pipeline& pipeline) = 0;

        /// The buffer must be in the VertexBuffer or Common state. A buffer in Common is promoted
        /// on first use (D3D12 implicit promotion), so a freshly uploaded buffer needs no barrier.
        /// Must be called after setPipeline(), which supplies the vertex stride.
        virtual void setVertexBuffer(Buffer& buffer, uint64_t offset = 0) = 0;

        /// Same state rule as setVertexBuffer(), with IndexBuffer instead of VertexBuffer.
        virtual void setIndexBuffer(Buffer& buffer, IndexFormat format, uint64_t offset = 0) = 0;

        /// Binds a texture to slot t<slot> of both shader stages, for the draws that follow in this
        /// pass. The texture must have Sampled usage and be in the ShaderRead state. Every slot a
        /// shader reads must be bound: Metal's API validation rejects a draw with a missing texture.
        virtual void setTexture(uint32_t slot, Texture& texture) = 0;

        /// Binds a Structured buffer to slot t<slot> of both shader stages, read as a StructuredBuffer,
        /// for the draws that follow in this pass. `slot` is at least kFirstStructuredBufferSlot. A
        /// GPU-only buffer must be in the ShaderRead or Common state (promoted on first use, as with
        /// vertex buffers); an Upload buffer may be bound as it is, but the CPU must not write the
        /// part a frame in flight still reads.
        virtual void setBuffer(uint32_t slot, Buffer& buffer) = 0;

        /// Binds a sampler to slot s<slot> of both shader stages. Same rules as setTexture().
        virtual void setSampler(uint32_t slot, Sampler& sampler) = 0;

        /// Copies `data` into this frame's upload ring and binds it to cbuffer slot b<slot> of both
        /// shader stages, for the draws that follow in this pass. The bytes must follow HLSL's
        /// cbuffer packing (16-byte rows; see ShaderConstants.h). Calling it again before the next
        /// draw replaces the binding; earlier draws keep the data they were recorded with. Slots
        /// nothing has set read as zero.
        void setConstants(uint32_t slot, std::span<const std::byte> data);

        template <typename T>
        void setConstants(uint32_t slot, const T& data)
        {
            static_assert(std::is_trivially_copyable_v<T>, "Constants are copied as raw bytes");
            setConstants(slot, std::as_bytes(std::span(&data, 1)));
        }

        virtual void draw(uint32_t vertexCount, uint32_t instanceCount = 1, uint32_t firstVertex = 0,
                          uint32_t firstInstance = 0) = 0;
        virtual void drawIndexed(uint32_t indexCount, uint32_t instanceCount = 1, uint32_t firstIndex = 0,
                                 int32_t baseVertex = 0, uint32_t firstInstance = 0) = 0;

        /// Copies bytes between buffers. Not allowed inside a render pass. The source must be in
        /// CopySource state (Upload buffers always are) and the destination in CopyDest state
        /// (Readback buffers always are).
        virtual void copyBuffer(Buffer& dst, uint64_t dstOffset, Buffer& src, uint64_t srcOffset, uint64_t size) = 0;

        /// Copies one whole mip level of one array layer (a cube face; 0 for a 2D texture) from a
        /// buffer into a texture. Row r of the image starts at srcOffset + r * srcRowPitch. srcOffset
        /// must be a multiple of kTextureCopyOffsetAlignment and srcRowPitch a multiple of
        /// kTextureCopyRowAlignment. The texture must be in CopyDest state and the buffer in
        /// CopySource state. Not allowed inside a render pass.
        virtual void copyBufferToTexture(Texture& dst, uint32_t mipLevel, uint32_t arrayLayer, Buffer& src,
                                         uint64_t srcOffset, uint32_t srcRowPitch) = 0;

        /// The reverse of copyBufferToTexture(), with the same layout rules. The texture must be in
        /// CopySource state and the buffer in CopyDest state.
        virtual void copyTextureToBuffer(Buffer& dst, uint64_t dstOffset, uint32_t dstRowPitch, Texture& src,
                                         uint32_t mipLevel, uint32_t arrayLayer) = 0;

        /// Moves a GPU-only buffer into the given state; does nothing if it is already there.
        /// Every buffer is back in Common at the start of each submission, so a buffer's first
        /// barrier in a command list starts from Common. Upload and Readback buffers have a fixed
        /// state and must not be passed here. Not allowed inside a render pass.
        virtual void barrier(Buffer& buffer, ResourceState state) = 0;

        /// Moves every mip level of a texture into the given state; does nothing if it is already
        /// there. Not allowed inside a render pass.
        virtual void barrier(Texture& texture, ResourceState state) = 0;

        /// Named regions shown in PIX, RenderDoc and the Xcode GPU debugger. Must be balanced
        /// within a frame, and within a render pass if pushed inside one.
        virtual void pushDebugGroup(std::string_view name) = 0;
        virtual void popDebugGroup() = 0;

        /// Called by the device once the command list has been submitted: every buffer that had a
        /// barrier in it goes back to Common. Those buffers must still be alive.
        void decayBufferStates();

    protected:
        CommandList() = default;

        /// Records the buffer's new state and remembers the buffer for decayBufferStates().
        void trackBufferState(Buffer& buffer, ResourceState state);

        /// Called by the device when the command list begins: where setConstants() allocates.
        void setUploadRing(UploadRing& ring, uint32_t region)
        {
            m_uploadRing = &ring;
            m_uploadRegion = region;
        }

        /// Binds `offset` bytes into `buffer` to cbuffer slot b<slot>. Only valid inside a render pass.
        virtual void bindConstants(uint32_t slot, Buffer& buffer, uint64_t offset) = 0;

        /// Asserts the rules shared by every backend; the backends call these first.
        static void validateTextureCopy(const Texture& texture, uint32_t mipLevel, uint32_t arrayLayer,
                                        ResourceState textureState, const Buffer& buffer, uint64_t bufferOffset,
                                        uint32_t rowPitch, ResourceState bufferState);
        static void validateTextureBinding(uint32_t slot, const Texture& texture);
        static void validateBufferBinding(uint32_t slot, const Buffer& buffer);

        /// Checks the attachments and remembers their formats for validatePipelineForPass().
        void validateRenderPass(const RenderPassDesc& desc);
        void validatePipelineForPass(const PipelineDesc& pipeline) const;
        static void validateSamplerBinding(uint32_t slot);

    private:
        std::vector<Buffer*> m_trackedBuffers;
        Format m_passColorFormat = Format::Unknown;
        Format m_passDepthFormat = Format::Unknown;
        UploadRing* m_uploadRing = nullptr;
        uint32_t m_uploadRegion = 0;
    };

    /// Pushes a debug group for the lifetime of the scope.
    class ScopedDebugGroup
    {
    public:
        ScopedDebugGroup(CommandList& commandList, std::string_view name) : m_commandList(commandList)
        {
            m_commandList.pushDebugGroup(name);
        }
        ~ScopedDebugGroup() { m_commandList.popDebugGroup(); }

        ScopedDebugGroup(const ScopedDebugGroup&) = delete;
        ScopedDebugGroup& operator=(const ScopedDebugGroup&) = delete;

    private:
        CommandList& m_commandList;
    };
}

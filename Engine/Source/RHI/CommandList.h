#pragma once

#include "RHI/RHITypes.h"

#include <string_view>
#include <vector>

namespace arc::rhi
{
    class Buffer;
    class Pipeline;
    class Sampler;
    class Texture;

    struct ColorAttachment
    {
        Texture* texture = nullptr;
        LoadOp load = LoadOp::Clear;
        StoreOp store = StoreOp::Store;
        ClearColor clearColor{};
    };

    struct RenderPassDesc
    {
        ColorAttachment color;

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
    /// beginRenderPass() moves the colour target to the RenderTarget state itself.
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

        /// Binds a sampler to slot s<slot> of both shader stages. Same rules as setTexture().
        virtual void setSampler(uint32_t slot, Sampler& sampler) = 0;

        virtual void draw(uint32_t vertexCount, uint32_t instanceCount = 1, uint32_t firstVertex = 0,
                          uint32_t firstInstance = 0) = 0;
        virtual void drawIndexed(uint32_t indexCount, uint32_t instanceCount = 1, uint32_t firstIndex = 0,
                                 int32_t baseVertex = 0, uint32_t firstInstance = 0) = 0;

        /// Copies bytes between buffers. Not allowed inside a render pass. The source must be in
        /// CopySource state (Upload buffers always are) and the destination in CopyDest state
        /// (Readback buffers always are).
        virtual void copyBuffer(Buffer& dst, uint64_t dstOffset, Buffer& src, uint64_t srcOffset, uint64_t size) = 0;

        /// Copies one whole mip level from a buffer into a texture. Row r of the image starts at
        /// srcOffset + r * srcRowPitch. srcOffset must be a multiple of kTextureCopyOffsetAlignment
        /// and srcRowPitch a multiple of kTextureCopyRowAlignment. The texture must be in CopyDest
        /// state and the buffer in CopySource state. Not allowed inside a render pass.
        virtual void copyBufferToTexture(Texture& dst, uint32_t mipLevel, Buffer& src, uint64_t srcOffset,
                                         uint32_t srcRowPitch) = 0;

        /// The reverse of copyBufferToTexture(), with the same layout rules. The texture must be in
        /// CopySource state and the buffer in CopyDest state.
        virtual void copyTextureToBuffer(Buffer& dst, uint64_t dstOffset, uint32_t dstRowPitch, Texture& src,
                                         uint32_t mipLevel) = 0;

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

        /// Asserts the rules shared by every backend; the backends call these first.
        static void validateTextureCopy(const Texture& texture, uint32_t mipLevel, ResourceState textureState,
                                        const Buffer& buffer, uint64_t bufferOffset, uint32_t rowPitch,
                                        ResourceState bufferState);
        static void validateTextureBinding(uint32_t slot, const Texture& texture);
        static void validateSamplerBinding(uint32_t slot);

    private:
        std::vector<Buffer*> m_trackedBuffers;
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

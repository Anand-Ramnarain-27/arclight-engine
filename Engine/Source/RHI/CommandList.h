#pragma once

#include "RHI/RHITypes.h"

#include <string_view>
#include <vector>

namespace arc::rhi
{
    class Buffer;
    class Texture;

    struct ColorAttachment
    {
        Texture* texture = nullptr;
        LoadOp load = LoadOp::Clear;
        StoreOp store = StoreOp::Store;
        ClearColor clearColor;
    };

    struct RenderPassDesc
    {
        ColorAttachment color;

        /// Shown as the pass's name in GPU debuggers. May be empty.
        std::string_view debugName;
    };

    /// Records GPU work for one frame. Obtained from Device::beginFrame() and valid until
    /// Device::endFrame().
    ///
    /// Draw commands may only be recorded between beginRenderPass() and endRenderPass(); copies
    /// and barriers only outside a pass. Passes do not nest. On Metal a pass is a render command
    /// encoder, on D3D12 it sets the render targets and performs the load action.
    class CommandList
    {
    public:
        virtual ~CommandList() = default;

        CommandList(const CommandList&) = delete;
        CommandList& operator=(const CommandList&) = delete;

        virtual void beginRenderPass(const RenderPassDesc& desc) = 0;
        virtual void endRenderPass() = 0;

        /// Copies bytes between buffers. Not allowed inside a render pass. The source must be in
        /// CopySource state (Upload buffers always are) and the destination in CopyDest state
        /// (Readback buffers always are).
        virtual void copyBuffer(Buffer& dst, uint64_t dstOffset, Buffer& src, uint64_t srcOffset, uint64_t size) = 0;

        /// Moves a GPU-only buffer into the given state; does nothing if it is already there.
        /// Every buffer is back in Common at the start of each submission, so a buffer's first
        /// barrier in a command list starts from Common. Upload and Readback buffers have a fixed
        /// state and must not be passed here. Not allowed inside a render pass.
        virtual void barrier(Buffer& buffer, ResourceState state) = 0;

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

#pragma once

#include "RHI/RHITypes.h"

#include <string_view>

namespace arc::rhi
{
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
    /// Draw commands may only be recorded between beginRenderPass() and endRenderPass(); passes
    /// do not nest. On Metal a pass is a render command encoder, on D3D12 it sets the render
    /// targets and performs the load action.
    class CommandList
    {
    public:
        virtual ~CommandList() = default;

        CommandList(const CommandList&) = delete;
        CommandList& operator=(const CommandList&) = delete;

        virtual void beginRenderPass(const RenderPassDesc& desc) = 0;
        virtual void endRenderPass() = 0;

        /// Named regions shown in PIX, RenderDoc and the Xcode GPU debugger. Must be balanced
        /// within a frame, and within a render pass if pushed inside one.
        virtual void pushDebugGroup(std::string_view name) = 0;
        virtual void popDebugGroup() = 0;

    protected:
        CommandList() = default;
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

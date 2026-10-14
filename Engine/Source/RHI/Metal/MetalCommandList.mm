#include "RHI/Metal/MetalBackend.h"

#include "Core/Assert.h"

#import <Foundation/Foundation.h>

namespace arc::rhi::metal
{
    namespace
    {
        NSString* toNSString(std::string_view text)
        {
            return [[NSString alloc] initWithBytes:text.data() length:text.size() encoding:NSUTF8StringEncoding];
        }

        MTLLoadAction toMTLLoadAction(LoadOp op)
        {
            switch (op)
            {
                case LoadOp::Load:     return MTLLoadActionLoad;
                case LoadOp::Clear:    return MTLLoadActionClear;
                case LoadOp::DontCare: return MTLLoadActionDontCare;
            }
            return MTLLoadActionDontCare;
        }

        MTLStoreAction toMTLStoreAction(StoreOp op)
        {
            return op == StoreOp::Store ? MTLStoreActionStore : MTLStoreActionDontCare;
        }
    }

    void MetalCommandList::begin(id<MTLCommandBuffer> commandBuffer)
    {
        ARC_ASSERT(m_commandBuffer == nil, "Command list begun twice");
        m_commandBuffer = commandBuffer;
    }

    id<MTLCommandBuffer> MetalCommandList::finish()
    {
        ARC_ASSERT(m_renderEncoder == nil, "Frame ended inside a render pass");
        ARC_ASSERT(m_debugGroupOnEncoder.empty(), "Frame ended with {} open debug group(s)", m_debugGroupOnEncoder.size());

        id<MTLCommandBuffer> commandBuffer = m_commandBuffer;
        m_commandBuffer = nil;
        return commandBuffer;
    }

    void MetalCommandList::beginRenderPass(const RenderPassDesc& desc)
    {
        ARC_ASSERT(m_renderEncoder == nil, "Render passes do not nest");
        ARC_ASSERT(desc.color.texture != nullptr);

        const auto& target = static_cast<const MetalTexture&>(*desc.color.texture);
        const ClearColor& c = desc.color.clearColor;

        MTLRenderPassDescriptor* pass = [MTLRenderPassDescriptor renderPassDescriptor];
        pass.colorAttachments[0].texture = target.native();
        pass.colorAttachments[0].loadAction = toMTLLoadAction(desc.color.load);
        pass.colorAttachments[0].storeAction = toMTLStoreAction(desc.color.store);
        pass.colorAttachments[0].clearColor = MTLClearColorMake(c.r, c.g, c.b, c.a);

        m_renderEncoder = [m_commandBuffer renderCommandEncoderWithDescriptor:pass];
        if (!desc.debugName.empty())
            m_renderEncoder.label = toNSString(desc.debugName);
        m_renderPassDescriptor = pass;
    }

    void MetalCommandList::endRenderPass()
    {
        ARC_ASSERT(m_renderEncoder != nil);
        ARC_ASSERT(m_debugGroupOnEncoder.empty() || !m_debugGroupOnEncoder.back(),
                   "Debug group pushed inside the render pass was not popped");

        [m_renderEncoder endEncoding];
        m_renderEncoder = nil;
        m_renderPassDescriptor = nil;
    }

    void MetalCommandList::pushDebugGroup(std::string_view name)
    {
        NSString* label = toNSString(name);
        const bool onEncoder = m_renderEncoder != nil;
        if (onEncoder)
            [m_renderEncoder pushDebugGroup:label];
        else
            [m_commandBuffer pushDebugGroup:label];
        m_debugGroupOnEncoder.push_back(onEncoder);
    }

    void MetalCommandList::popDebugGroup()
    {
        ARC_ASSERT(!m_debugGroupOnEncoder.empty(), "Unbalanced popDebugGroup");
        if (m_debugGroupOnEncoder.back())
            [m_renderEncoder popDebugGroup];
        else
            [m_commandBuffer popDebugGroup];
        m_debugGroupOnEncoder.pop_back();
    }
}

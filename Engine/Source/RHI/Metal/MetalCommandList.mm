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
        endBlitEncoder();
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
        endBlitEncoder();

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

    id<MTLBlitCommandEncoder> MetalCommandList::blitEncoder()
    {
        if (!m_blitEncoder)
            m_blitEncoder = [m_commandBuffer blitCommandEncoder];
        return m_blitEncoder;
    }

    void MetalCommandList::endBlitEncoder()
    {
        if (!m_blitEncoder)
            return;
        [m_blitEncoder endEncoding];
        m_blitEncoder = nil;
    }

    void MetalCommandList::copyBuffer(Buffer& dst, uint64_t dstOffset, Buffer& src, uint64_t srcOffset, uint64_t size)
    {
        ARC_ASSERT(m_renderEncoder == nil, "Copies are not allowed inside a render pass");
        ARC_ASSERT(dst.state() == ResourceState::CopyDest, "Copy destination '{}' is in state {}",
                   dst.desc().debugName, resourceStateName(dst.state()));
        ARC_ASSERT(src.state() == ResourceState::CopySource, "Copy source '{}' is in state {}",
                   src.desc().debugName, resourceStateName(src.state()));
        ARC_ASSERT(dstOffset + size <= dst.desc().size && srcOffset + size <= src.desc().size);

        [blitEncoder() copyFromBuffer:static_cast<MetalBuffer&>(src).native()
                         sourceOffset:srcOffset
                             toBuffer:static_cast<MetalBuffer&>(dst).native()
                    destinationOffset:dstOffset
                                 size:size];
    }

    void MetalCommandList::barrier(Buffer& buffer, ResourceState state)
    {
        ARC_ASSERT(m_renderEncoder == nil, "Barriers are not allowed inside a render pass");
        ARC_ASSERT(buffer.desc().memory == MemoryType::GpuOnly, "'{}' has a fixed state", buffer.desc().debugName);
        // Buffers are created with Metal's default hazard tracking, so Metal inserts the
        // synchronisation itself. Only the tracked state changes, which keeps the asserts in
        // copyBuffer() meaningful on both backends.
        trackBufferState(buffer, state);
    }

    void MetalCommandList::pushDebugGroup(std::string_view name)
    {
        endBlitEncoder();
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
        endBlitEncoder();
        if (m_debugGroupOnEncoder.back())
            [m_renderEncoder popDebugGroup];
        else
            [m_commandBuffer popDebugGroup];
        m_debugGroupOnEncoder.pop_back();
    }
}

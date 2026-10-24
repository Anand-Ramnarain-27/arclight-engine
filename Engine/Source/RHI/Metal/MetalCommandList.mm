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

    void MetalCommandList::begin(id<MTLCommandBuffer> commandBuffer, UploadRing& ring, uint32_t region)
    {
        ARC_ASSERT(m_commandBuffer == nil, "Command list begun twice");
        m_commandBuffer = commandBuffer;
        setUploadRing(ring, region);
        m_zeroConstants = ring.zeroBlock();
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
        validateRenderPass(desc);
        endBlitEncoder();

        auto& target = static_cast<MetalTexture&>(*desc.color.texture);
        // Metal tracks the hazard itself; the state is kept so the RHI's rules hold on every backend.
        target.setState(ResourceState::RenderTarget);
        const ClearColor& c = desc.color.clearColor;

        MTLRenderPassDescriptor* pass = [MTLRenderPassDescriptor renderPassDescriptor];
        pass.colorAttachments[0].texture = target.native();
        pass.colorAttachments[0].loadAction = toMTLLoadAction(desc.color.load);
        pass.colorAttachments[0].storeAction = toMTLStoreAction(desc.color.store);
        pass.colorAttachments[0].clearColor = MTLClearColorMake(c.r, c.g, c.b, c.a);
        if (auto* depth = static_cast<MetalTexture*>(desc.depth.texture))
        {
            depth->setState(ResourceState::DepthWrite);
            pass.depthAttachment.texture = depth->native();
            pass.depthAttachment.loadAction = toMTLLoadAction(desc.depth.load);
            pass.depthAttachment.storeAction = toMTLStoreAction(desc.depth.store);
            pass.depthAttachment.clearDepth = desc.depth.clearDepth;
        }

        m_renderEncoder = [m_commandBuffer renderCommandEncoderWithDescriptor:pass];
        if (!desc.debugName.empty())
            m_renderEncoder.label = toNSString(desc.debugName);
        m_renderPassDescriptor = pass;

        // A new encoder has nothing bound, and Metal's validation rejects a draw whose shader reads
        // an unbound buffer, so every constant slot starts on the zero block (D3D12 does the same).
        for (uint32_t slot = 0; slot < kMaxConstantSlots; ++slot)
            bindConstants(slot, *m_zeroConstants.buffer, m_zeroConstants.offset);
    }

    void MetalCommandList::bindConstants(uint32_t slot, Buffer& buffer, uint64_t offset)
    {
        ARC_ASSERT(m_renderEncoder != nil, "setConstants outside a render pass");
        id<MTLBuffer> native = static_cast<MetalBuffer&>(buffer).native();
        [m_renderEncoder setVertexBuffer:native offset:offset atIndex:slot];
        [m_renderEncoder setFragmentBuffer:native offset:offset atIndex:slot];
    }

    void MetalCommandList::endRenderPass()
    {
        ARC_ASSERT(m_renderEncoder != nil);
        ARC_ASSERT(m_debugGroupOnEncoder.empty() || !m_debugGroupOnEncoder.back(),
                   "Debug group pushed inside the render pass was not popped");

        [m_renderEncoder endEncoding];
        m_renderEncoder = nil;
        m_renderPassDescriptor = nil;
        m_indexBuffer = nil;
        m_hasPipeline = false;
    }

    void MetalCommandList::setPipeline(Pipeline& pipeline)
    {
        ARC_ASSERT(m_renderEncoder != nil, "setPipeline outside a render pass");
        validatePipelineForPass(pipeline.desc());
        const auto& metalPipeline = static_cast<const MetalPipeline&>(pipeline);
        const PipelineDesc& desc = metalPipeline.desc();

        [m_renderEncoder setRenderPipelineState:metalPipeline.state()];
        [m_renderEncoder setDepthStencilState:metalPipeline.depthState()];
        // Culling and winding are encoder state on Metal but part of the pipeline in D3D12, so
        // they are applied together with the pipeline to keep the RHI's meaning.
        [m_renderEncoder setCullMode:desc.cullMode == CullMode::None  ? MTLCullModeNone
                                   : desc.cullMode == CullMode::Front ? MTLCullModeFront
                                                                      : MTLCullModeBack];
        [m_renderEncoder setFrontFacingWinding:desc.frontCounterClockwise ? MTLWindingCounterClockwise
                                                                          : MTLWindingClockwise];
        m_hasPipeline = true;
    }

    void MetalCommandList::setVertexBuffer(Buffer& buffer, uint64_t offset)
    {
        ARC_ASSERT(m_hasPipeline, "setVertexBuffer before setPipeline");
        ARC_ASSERT(buffer.state() == ResourceState::Common || buffer.state() == ResourceState::VertexBuffer,
                   "Vertex buffer '{}' is in state {}", buffer.desc().debugName, resourceStateName(buffer.state()));
        ARC_ASSERT(offset < buffer.desc().size);
        [m_renderEncoder setVertexBuffer:static_cast<MetalBuffer&>(buffer).native() offset:offset atIndex:kVertexBufferIndex];
    }

    void MetalCommandList::setIndexBuffer(Buffer& buffer, IndexFormat format, uint64_t offset)
    {
        ARC_ASSERT(m_renderEncoder != nil, "setIndexBuffer outside a render pass");
        ARC_ASSERT(buffer.state() == ResourceState::Common || buffer.state() == ResourceState::IndexBuffer,
                   "Index buffer '{}' is in state {}", buffer.desc().debugName, resourceStateName(buffer.state()));
        ARC_ASSERT(offset < buffer.desc().size);
        m_indexBuffer = static_cast<MetalBuffer&>(buffer).native();
        m_indexBufferOffset = offset;
        m_indexType = format == IndexFormat::UInt16 ? MTLIndexTypeUInt16 : MTLIndexTypeUInt32;
    }

    void MetalCommandList::setTexture(uint32_t slot, Texture& texture)
    {
        ARC_ASSERT(m_renderEncoder != nil, "setTexture outside a render pass");
        validateTextureBinding(slot, texture);
        // The shader build passes --msl-decoration-binding to SPIRV-Cross, so register(t<slot>)
        // becomes [[texture(<slot>)]] in both stages.
        id<MTLTexture> native = static_cast<MetalTexture&>(texture).native();
        [m_renderEncoder setVertexTexture:native atIndex:slot];
        [m_renderEncoder setFragmentTexture:native atIndex:slot];
    }

    void MetalCommandList::setBuffer(uint32_t slot, Buffer& buffer)
    {
        ARC_ASSERT(m_renderEncoder != nil, "setBuffer outside a render pass");
        validateBufferBinding(slot, buffer);
        // SPIRV-Cross turns a StructuredBuffer in register(t<slot>) into a device buffer argument at
        // [[buffer(<slot>)]], kept equal to the register by --msl-decoration-binding.
        id<MTLBuffer> native = static_cast<MetalBuffer&>(buffer).native();
        [m_renderEncoder setVertexBuffer:native offset:0 atIndex:slot];
        [m_renderEncoder setFragmentBuffer:native offset:0 atIndex:slot];
    }

    void MetalCommandList::setSampler(uint32_t slot, Sampler& sampler)
    {
        ARC_ASSERT(m_renderEncoder != nil, "setSampler outside a render pass");
        validateSamplerBinding(slot);
        id<MTLSamplerState> native = static_cast<MetalSampler&>(sampler).native();
        [m_renderEncoder setVertexSamplerState:native atIndex:slot];
        [m_renderEncoder setFragmentSamplerState:native atIndex:slot];
    }

    void MetalCommandList::draw(uint32_t vertexCount, uint32_t instanceCount, uint32_t firstVertex, uint32_t firstInstance)
    {
        ARC_ASSERT(m_hasPipeline, "Draw without a pipeline");
        [m_renderEncoder drawPrimitives:MTLPrimitiveTypeTriangle
                            vertexStart:firstVertex
                            vertexCount:vertexCount
                          instanceCount:instanceCount
                           baseInstance:firstInstance];
    }

    void MetalCommandList::drawIndexed(uint32_t indexCount, uint32_t instanceCount, uint32_t firstIndex,
                                       int32_t baseVertex, uint32_t firstInstance)
    {
        ARC_ASSERT(m_hasPipeline, "Draw without a pipeline");
        ARC_ASSERT(m_indexBuffer != nil, "drawIndexed without an index buffer");
        const NSUInteger indexSize = m_indexType == MTLIndexTypeUInt16 ? 2 : 4;
        [m_renderEncoder drawIndexedPrimitives:MTLPrimitiveTypeTriangle
                                    indexCount:indexCount
                                     indexType:m_indexType
                                   indexBuffer:m_indexBuffer
                             indexBufferOffset:m_indexBufferOffset + firstIndex * indexSize
                                 instanceCount:instanceCount
                                    baseVertex:baseVertex
                                  baseInstance:firstInstance];
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

    void MetalCommandList::copyBufferToTexture(Texture& dst, uint32_t mipLevel, uint32_t arrayLayer, Buffer& src,
                                               uint64_t srcOffset, uint32_t srcRowPitch)
    {
        ARC_ASSERT(m_renderEncoder == nil, "Copies are not allowed inside a render pass");
        validateTextureCopy(dst, mipLevel, arrayLayer, ResourceState::CopyDest, src, srcOffset, srcRowPitch,
                            ResourceState::CopySource);

        const Extent2D size = mipSize(dst.desc().size, mipLevel);
        [blitEncoder() copyFromBuffer:static_cast<MetalBuffer&>(src).native()
                         sourceOffset:srcOffset
                    sourceBytesPerRow:srcRowPitch
                  sourceBytesPerImage:NSUInteger{srcRowPitch} * size.height
                           sourceSize:MTLSizeMake(size.width, size.height, 1)
                            toTexture:static_cast<MetalTexture&>(dst).native()
                     destinationSlice:arrayLayer
                     destinationLevel:mipLevel
                    destinationOrigin:MTLOriginMake(0, 0, 0)];
    }

    void MetalCommandList::copyTextureToBuffer(Buffer& dst, uint64_t dstOffset, uint32_t dstRowPitch, Texture& src,
                                               uint32_t mipLevel, uint32_t arrayLayer)
    {
        ARC_ASSERT(m_renderEncoder == nil, "Copies are not allowed inside a render pass");
        validateTextureCopy(src, mipLevel, arrayLayer, ResourceState::CopySource, dst, dstOffset, dstRowPitch,
                            ResourceState::CopyDest);

        const Extent2D size = mipSize(src.desc().size, mipLevel);
        [blitEncoder() copyFromTexture:static_cast<MetalTexture&>(src).native()
                           sourceSlice:arrayLayer
                           sourceLevel:mipLevel
                          sourceOrigin:MTLOriginMake(0, 0, 0)
                            sourceSize:MTLSizeMake(size.width, size.height, 1)
                              toBuffer:static_cast<MetalBuffer&>(dst).native()
                     destinationOffset:dstOffset
                destinationBytesPerRow:dstRowPitch
              destinationBytesPerImage:NSUInteger{dstRowPitch} * size.height];
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

    void MetalCommandList::barrier(Texture& texture, ResourceState state)
    {
        ARC_ASSERT(m_renderEncoder == nil, "Barriers are not allowed inside a render pass");
        // Hazard tracking covers textures as well as buffers; see barrier(Buffer&).
        texture.setState(state);
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

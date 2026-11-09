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

    id<MTLTexture> MetalTexture::mipView(uint32_t mipLevel)
    {
        if (m_mipViews.size() < m_desc.mipLevels)
            m_mipViews.resize(m_desc.mipLevels);
        // A reference to an ARC pointer needs an explicit ownership qualifier.
        __strong id<MTLTexture>& view = m_mipViews[mipLevel];
        if (!view)
            view = [m_texture newTextureViewWithPixelFormat:m_texture.pixelFormat
                                                textureType:MTLTextureType2D
                                                     levels:NSMakeRange(mipLevel, 1)
                                                     slices:NSMakeRange(0, 1)];
        return view;
    }

    void MetalCommandList::begin(id<MTLCommandBuffer> commandBuffer, UploadRing& ring, uint32_t region,
                                 GpuTimingRecorder* timing, id<MTLCounterSampleBuffer> samples, uint32_t firstSample,
                                 ComputePassFlags* computePasses)
    {
        ARC_ASSERT(m_commandBuffer == nil, "Command list begun twice");
        m_commandBuffer = commandBuffer;
        m_timing = samples != nil ? timing : nullptr;
        m_timestampSamples = samples;
        m_firstSample = firstSample;
        m_computePasses = computePasses;
        setUploadRing(ring, region);
        m_zeroConstants = ring.zeroBlock();
    }

    id<MTLCommandBuffer> MetalCommandList::finish()
    {
        endBlitEncoder();
        ARC_ASSERT(!inPass(), "Frame ended inside a pass");
        ARC_ASSERT(m_debugGroupOnEncoder.empty(), "Frame ended with {} open debug group(s)", m_debugGroupOnEncoder.size());

        id<MTLCommandBuffer> commandBuffer = m_commandBuffer;
        m_commandBuffer = nil;
        m_timing = nullptr;
        m_timestampSamples = nil;
        m_computePasses = nullptr;
        return commandBuffer;
    }

    void MetalCommandList::beginRenderPass(const RenderPassDesc& desc)
    {
        ARC_ASSERT(!inPass(), "Passes do not nest");
        validateRenderPass(desc);
        endBlitEncoder();

        auto& target = static_cast<MetalTexture&>(*desc.color.texture);
        // Metal tracks the hazard itself; the state is kept so the RHI's rules hold on every backend.
        target.setState(ResourceState::RenderTarget);
        const ClearColor& c = desc.color.clearColor;

        MTLRenderPassDescriptor* pass = [MTLRenderPassDescriptor renderPassDescriptor];
        pass.colorAttachments[0].texture = target.native();
        pass.colorAttachments[0].level = desc.color.mipLevel;
        pass.colorAttachments[0].slice = desc.color.arrayLayer;
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

        // Apple GPUs can only sample counters at stage boundaries, which is why passes are the unit of
        // GPU timing. A pass with no draws may not run its vertex stage, so both stages are sampled.
        m_timedPass = m_timing ? m_timing->beginPass(desc.debugName) : GpuTimingRecorder::kNoPass;
        if (m_timedPass != GpuTimingRecorder::kNoPass)
        {
            if (m_computePasses)
                (*m_computePasses)[m_timedPass] = false;
            const NSUInteger first = m_firstSample + kTimestampSamplesPerPass * m_timedPass;
            MTLRenderPassSampleBufferAttachmentDescriptor* sampling = pass.sampleBufferAttachments[0];
            sampling.sampleBuffer = m_timestampSamples;
            sampling.startOfVertexSampleIndex = first;
            sampling.endOfVertexSampleIndex = first + 1;
            sampling.startOfFragmentSampleIndex = first + 2;
            sampling.endOfFragmentSampleIndex = first + 3;
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
        ARC_ASSERT(inPass(), "setConstants outside a pass");
        id<MTLBuffer> native = static_cast<MetalBuffer&>(buffer).native();
        if (m_computeEncoder)
        {
            [m_computeEncoder setBuffer:native offset:offset atIndex:slot];
            return;
        }
        [m_renderEncoder setVertexBuffer:native offset:offset atIndex:slot];
        [m_renderEncoder setFragmentBuffer:native offset:offset atIndex:slot];
    }

    void MetalCommandList::beginComputePass(const ComputePassDesc& desc)
    {
        ARC_ASSERT(!inPass(), "Passes do not nest");
        endBlitEncoder();

        // Serial: each dispatch finishes before the next starts, so storage writes need no barrier
        // within the pass (see storageBarrier()).
        MTLComputePassDescriptor* pass = [MTLComputePassDescriptor computePassDescriptor];
        pass.dispatchType = MTLDispatchTypeSerial;
        m_timedPass = m_timing ? m_timing->beginPass(desc.debugName) : GpuTimingRecorder::kNoPass;
        if (m_timedPass != GpuTimingRecorder::kNoPass)
        {
            if (m_computePasses)
                (*m_computePasses)[m_timedPass] = true;
            const NSUInteger first = m_firstSample + kTimestampSamplesPerPass * m_timedPass;
            MTLComputePassSampleBufferAttachmentDescriptor* sampling = pass.sampleBufferAttachments[0];
            sampling.sampleBuffer = m_timestampSamples;
            sampling.startOfEncoderSampleIndex = first;
            sampling.endOfEncoderSampleIndex = first + 1;
        }

        m_computeEncoder = [m_commandBuffer computeCommandEncoderWithDescriptor:pass];
        if (!desc.debugName.empty())
            m_computeEncoder.label = toNSString(desc.debugName);
        for (uint32_t slot = 0; slot < kMaxConstantSlots; ++slot)
            bindConstants(slot, *m_zeroConstants.buffer, m_zeroConstants.offset);
    }

    void MetalCommandList::endComputePass()
    {
        ARC_ASSERT(m_computeEncoder != nil);
        ARC_ASSERT(m_debugGroupOnEncoder.empty() || !m_debugGroupOnEncoder.back(),
                   "Debug group pushed inside the compute pass was not popped");
        [m_computeEncoder endEncoding];
        m_computeEncoder = nil;
        m_computePipeline = nullptr;
        if (m_timing)
            m_timing->endPass(m_timedPass);
        m_timedPass = GpuTimingRecorder::kNoPass;
    }

    void MetalCommandList::setComputePipeline(ComputePipeline& pipeline)
    {
        ARC_ASSERT(m_computeEncoder != nil, "setComputePipeline outside a compute pass");
        m_computePipeline = &static_cast<MetalComputePipeline&>(pipeline);
        [m_computeEncoder setComputePipelineState:m_computePipeline->state()];
    }

    void MetalCommandList::setStorageBuffer(uint32_t slot, Buffer& buffer)
    {
        ARC_ASSERT(m_computeEncoder != nil, "setStorageBuffer outside a compute pass");
        validateStorageBufferBinding(slot, buffer);
        [m_computeEncoder setBuffer:static_cast<MetalBuffer&>(buffer).native() offset:0 atIndex:kStorageSlotOffset + slot];
    }

    void MetalCommandList::setStorageTexture(uint32_t slot, Texture& texture, uint32_t mipLevel)
    {
        ARC_ASSERT(m_computeEncoder != nil, "setStorageTexture outside a compute pass");
        validateStorageTextureBinding(slot, texture, mipLevel);
        auto& metalTexture = static_cast<MetalTexture&>(texture);
        id<MTLTexture> native = metalTexture.desc().mipLevels == 1 ? metalTexture.native() : metalTexture.mipView(mipLevel);
        [m_computeEncoder setTexture:native atIndex:kStorageSlotOffset + slot];
    }

    void MetalCommandList::dispatch(uint32_t groupsX, uint32_t groupsY, uint32_t groupsZ)
    {
        ARC_ASSERT(m_computeEncoder != nil, "Dispatch outside a compute pass");
        validateDispatch(m_computePipeline, groupsX, groupsY, groupsZ);
        // Metal takes the group size with every dispatch; D3D12 reads it from the shader.
        const ThreadGroupSize& group = m_computePipeline->desc().threadGroupSize;
        [m_computeEncoder dispatchThreadgroups:MTLSizeMake(groupsX, groupsY, groupsZ)
                         threadsPerThreadgroup:MTLSizeMake(group.x, group.y, group.z)];
    }

    void MetalCommandList::storageBarrier()
    {
        ARC_ASSERT(m_computeEncoder != nil, "storageBarrier outside a compute pass");
        // Nothing to do: the pass's encoder is serial, so each dispatch sees the writes of the ones
        // before it.
    }

    id<MTLCommandEncoder> MetalCommandList::passEncoder() const
    {
        if (m_renderEncoder)
            return m_renderEncoder;
        return m_computeEncoder;
    }

    void MetalCommandList::endRenderPass()
    {
        ARC_ASSERT(m_renderEncoder != nil);
        ARC_ASSERT(m_debugGroupOnEncoder.empty() || !m_debugGroupOnEncoder.back(),
                   "Debug group pushed inside the render pass was not popped");

        [m_renderEncoder endEncoding];
        m_renderEncoder = nil;
        m_renderPassDescriptor = nil;
        if (m_timing)
            m_timing->endPass(m_timedPass);
        m_timedPass = GpuTimingRecorder::kNoPass;
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
        ARC_ASSERT(inPass(), "setTexture outside a pass");
        validateTextureBinding(slot, texture);
        // The shader build passes --msl-decoration-binding to SPIRV-Cross, so register(t<slot>)
        // becomes [[texture(<slot>)]] in both stages.
        id<MTLTexture> native = static_cast<MetalTexture&>(texture).native();
        if (m_computeEncoder)
        {
            [m_computeEncoder setTexture:native atIndex:slot];
            return;
        }
        [m_renderEncoder setVertexTexture:native atIndex:slot];
        [m_renderEncoder setFragmentTexture:native atIndex:slot];
    }

    void MetalCommandList::setBuffer(uint32_t slot, Buffer& buffer)
    {
        ARC_ASSERT(inPass(), "setBuffer outside a pass");
        validateBufferBinding(slot, buffer);
        // SPIRV-Cross turns a StructuredBuffer in register(t<slot>) into a device buffer argument at
        // [[buffer(<slot>)]], kept equal to the register by --msl-decoration-binding.
        id<MTLBuffer> native = static_cast<MetalBuffer&>(buffer).native();
        if (m_computeEncoder)
        {
            [m_computeEncoder setBuffer:native offset:0 atIndex:slot];
            return;
        }
        [m_renderEncoder setVertexBuffer:native offset:0 atIndex:slot];
        [m_renderEncoder setFragmentBuffer:native offset:0 atIndex:slot];
    }

    void MetalCommandList::setSampler(uint32_t slot, Sampler& sampler)
    {
        ARC_ASSERT(inPass(), "setSampler outside a pass");
        validateSamplerBinding(slot);
        id<MTLSamplerState> native = static_cast<MetalSampler&>(sampler).native();
        if (m_computeEncoder)
        {
            [m_computeEncoder setSamplerState:native atIndex:slot];
            return;
        }
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
        ARC_ASSERT(!inPass(), "Copies are not allowed inside a pass");
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
        ARC_ASSERT(!inPass(), "Copies are not allowed inside a pass");
        validateTextureCopy(dst, mipLevel, arrayLayer, ResourceState::CopyDest, src, srcOffset, srcRowPitch,
                            ResourceState::CopySource);

        // Metal takes the level's own size in pixels, even when a block-compressed level is smaller
        // than a block; the bytes per image count rows of blocks.
        const Extent2D size = mipSize(dst.desc().size, mipLevel);
        [blitEncoder() copyFromBuffer:static_cast<MetalBuffer&>(src).native()
                         sourceOffset:srcOffset
                    sourceBytesPerRow:srcRowPitch
                  sourceBytesPerImage:NSUInteger{srcRowPitch} * rowCount(dst.desc().format, size.height)
                           sourceSize:MTLSizeMake(size.width, size.height, 1)
                            toTexture:static_cast<MetalTexture&>(dst).native()
                     destinationSlice:arrayLayer
                     destinationLevel:mipLevel
                    destinationOrigin:MTLOriginMake(0, 0, 0)];
    }

    void MetalCommandList::copyTextureToBuffer(Buffer& dst, uint64_t dstOffset, uint32_t dstRowPitch, Texture& src,
                                               uint32_t mipLevel, uint32_t arrayLayer)
    {
        ARC_ASSERT(!inPass(), "Copies are not allowed inside a pass");
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
              destinationBytesPerImage:NSUInteger{dstRowPitch} * rowCount(src.desc().format, size.height)];
    }

    void MetalCommandList::barrier(Buffer& buffer, ResourceState state)
    {
        ARC_ASSERT(!inPass(), "Barriers are not allowed inside a pass");
        ARC_ASSERT(buffer.desc().memory == MemoryType::GpuOnly, "'{}' has a fixed state", buffer.desc().debugName);
        // Buffers are created with Metal's default hazard tracking, so Metal inserts the
        // synchronisation itself. Only the tracked state changes, which keeps the asserts in
        // copyBuffer() meaningful on both backends.
        trackBufferState(buffer, state);
    }

    void MetalCommandList::barrier(Texture& texture, ResourceState state)
    {
        ARC_ASSERT(!inPass(), "Barriers are not allowed inside a pass");
        // Hazard tracking covers textures as well as buffers; see barrier(Buffer&).
        texture.setState(state);
    }

    void MetalCommandList::pushDebugGroup(std::string_view name)
    {
        endBlitEncoder();
        NSString* label = toNSString(name);
        const bool onEncoder = inPass();
        if (onEncoder)
            [passEncoder() pushDebugGroup:label];
        else
            [m_commandBuffer pushDebugGroup:label];
        m_debugGroupOnEncoder.push_back(onEncoder);
    }

    void MetalCommandList::popDebugGroup()
    {
        ARC_ASSERT(!m_debugGroupOnEncoder.empty(), "Unbalanced popDebugGroup");
        endBlitEncoder();
        if (m_debugGroupOnEncoder.back())
            [passEncoder() popDebugGroup];
        else
            [m_commandBuffer popDebugGroup];
        m_debugGroupOnEncoder.pop_back();
    }
}

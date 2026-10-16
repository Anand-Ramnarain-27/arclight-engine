#include "RHI/D3D12/D3D12Backend.h"

#include "Core/Assert.h"

#include <string>

namespace arc::rhi::d3d12
{
    namespace
    {
        // PIX and RenderDoc decode BeginEvent payloads by this metadata value; 1 means a
        // null-terminated ANSI string (WINPIX_EVENT_ANSI_VERSION in pix3.h).
        constexpr UINT kEventMetadataAnsi = 1;

        void beginEvent(ID3D12GraphicsCommandList* list, std::string_view name)
        {
            const std::string text(name);
            list->BeginEvent(kEventMetadataAnsi, text.c_str(), static_cast<UINT>(text.size() + 1));
        }
    }

    D3D12_RESOURCE_STATES toD3D12State(ResourceState state)
    {
        switch (state)
        {
            case ResourceState::Common:         return D3D12_RESOURCE_STATE_COMMON;
            case ResourceState::CopySource:     return D3D12_RESOURCE_STATE_COPY_SOURCE;
            case ResourceState::CopyDest:       return D3D12_RESOURCE_STATE_COPY_DEST;
            case ResourceState::VertexBuffer:
            case ResourceState::ConstantBuffer: return D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER;
            case ResourceState::IndexBuffer:    return D3D12_RESOURCE_STATE_INDEX_BUFFER;
            case ResourceState::ShaderRead:
                return D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
            case ResourceState::RenderTarget:   return D3D12_RESOURCE_STATE_RENDER_TARGET;
            case ResourceState::DepthWrite:     return D3D12_RESOURCE_STATE_DEPTH_WRITE;
            case ResourceState::Present:        return D3D12_RESOURCE_STATE_PRESENT;
        }
        return D3D12_RESOURCE_STATE_COMMON;
    }

    bool D3D12CommandList::init(D3D12Device& device, ID3D12CommandAllocator* allocator, std::string_view name)
    {
        m_device = &device;
        if (!succeeded(device.native()->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, allocator, nullptr,
                                                          IID_PPV_ARGS(&m_list)),
                       "CreateCommandList"))
            return false;
        setDebugName(m_list.Get(), name);

        // Command lists are created open; begin() expects a closed one.
        return close();
    }

    void D3D12CommandList::begin(ID3D12CommandAllocator* allocator, uint32_t transientRegion)
    {
        ARC_VERIFY(SUCCEEDED(m_list->Reset(allocator, nullptr)));
        m_transientRegion = transientRegion;

        // Set once per command list: changing heaps mid-list can stall the GPU on some hardware,
        // which is why everything shaders read lives in these two heaps.
        ID3D12DescriptorHeap* heaps[] = {m_device->shaderVisibleSrvHeap().native(),
                                         m_device->shaderVisibleSamplerHeap().native()};
        m_list->SetDescriptorHeaps(2, heaps);
        m_rootSignature = nullptr;
        setUploadRing(m_device->uploadRing(), transientRegion);
        resetBindings();
    }

    void D3D12CommandList::resetBindings()
    {
        m_textures.fill(m_device->nullSrv());
        m_samplers.fill(m_device->defaultSampler());
        const UploadAllocation zeros = m_device->uploadRing().zeroBlock();
        m_constants.fill(static_cast<D3D12Buffer&>(*zeros.buffer).gpuAddress() + zeros.offset);
        m_texturesDirty = true;
        m_samplersDirty = true;
        m_constantsDirty = (1u << kMaxConstantSlots) - 1;
    }

    void D3D12CommandList::invalidateBindings()
    {
        m_rootSignature = nullptr;
        m_pipeline = nullptr;
        m_texturesDirty = true;
        m_samplersDirty = true;
        m_constantsDirty = (1u << kMaxConstantSlots) - 1;
    }

    bool D3D12CommandList::close()
    {
        ARC_ASSERT(!m_inRenderPass, "Command list closed inside a render pass");
        ARC_ASSERT(m_debugDepth == 0, "Command list closed with {} open debug group(s)", m_debugDepth);
        return succeeded(m_list->Close(), "ID3D12GraphicsCommandList::Close");
    }

    void D3D12CommandList::setPipeline(Pipeline& pipeline)
    {
        ARC_ASSERT(m_inRenderPass, "setPipeline outside a render pass");
        validatePipelineForPass(pipeline.desc());
        m_pipeline = &static_cast<D3D12Pipeline&>(pipeline);
        m_list->SetPipelineState(m_pipeline->state());

        // Setting a different root signature invalidates every root argument, so the tables
        // are rebuilt at the next draw. Every pipeline currently shares one, so this happens once
        // per list, or after ImGui has drawn with its own.
        if (m_rootSignature != m_pipeline->rootSignature())
        {
            m_rootSignature = m_pipeline->rootSignature();
            m_list->SetGraphicsRootSignature(m_rootSignature);
            m_texturesDirty = true;
            m_samplersDirty = true;
            m_constantsDirty = (1u << kMaxConstantSlots) - 1;
        }
        // The PSO only fixes the topology type (triangles); the exact topology is command list state.
        m_list->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    }

    void D3D12CommandList::setVertexBuffer(Buffer& buffer, uint64_t offset)
    {
        ARC_ASSERT(m_pipeline, "setVertexBuffer before setPipeline");
        ARC_ASSERT(buffer.state() == ResourceState::Common || buffer.state() == ResourceState::VertexBuffer,
                   "Vertex buffer '{}' is in state {}", buffer.desc().debugName, resourceStateName(buffer.state()));
        ARC_ASSERT(offset < buffer.desc().size);

        D3D12_VERTEX_BUFFER_VIEW view{};
        view.BufferLocation = static_cast<D3D12Buffer&>(buffer).gpuAddress() + offset;
        view.SizeInBytes = static_cast<UINT>(buffer.desc().size - offset);
        view.StrideInBytes = m_pipeline->desc().vertexLayout.stride;
        m_list->IASetVertexBuffers(0, 1, &view);
    }

    void D3D12CommandList::setIndexBuffer(Buffer& buffer, IndexFormat format, uint64_t offset)
    {
        ARC_ASSERT(m_inRenderPass, "setIndexBuffer outside a render pass");
        ARC_ASSERT(buffer.state() == ResourceState::Common || buffer.state() == ResourceState::IndexBuffer,
                   "Index buffer '{}' is in state {}", buffer.desc().debugName, resourceStateName(buffer.state()));
        ARC_ASSERT(offset < buffer.desc().size);

        D3D12_INDEX_BUFFER_VIEW view{};
        view.BufferLocation = static_cast<D3D12Buffer&>(buffer).gpuAddress() + offset;
        view.SizeInBytes = static_cast<UINT>(buffer.desc().size - offset);
        view.Format = format == IndexFormat::UInt16 ? DXGI_FORMAT_R16_UINT : DXGI_FORMAT_R32_UINT;
        m_list->IASetIndexBuffer(&view);
    }

    void D3D12CommandList::setTexture(uint32_t slot, Texture& texture)
    {
        ARC_ASSERT(m_inRenderPass, "setTexture outside a render pass");
        validateTextureBinding(slot, texture);
        const D3D12_CPU_DESCRIPTOR_HANDLE srv = static_cast<D3D12Texture&>(texture).srv;
        if (m_textures[slot].ptr != srv.ptr)
        {
            m_textures[slot] = srv;
            m_texturesDirty = true;
        }
    }

    void D3D12CommandList::setSampler(uint32_t slot, Sampler& sampler)
    {
        ARC_ASSERT(m_inRenderPass, "setSampler outside a render pass");
        validateSamplerBinding(slot);
        const D3D12_CPU_DESCRIPTOR_HANDLE descriptor = static_cast<D3D12Sampler&>(sampler).descriptor();
        if (m_samplers[slot].ptr != descriptor.ptr)
        {
            m_samplers[slot] = descriptor;
            m_samplersDirty = true;
        }
    }

    void D3D12CommandList::bindConstants(uint32_t slot, Buffer& buffer, uint64_t offset)
    {
        ARC_ASSERT(m_inRenderPass, "setConstants outside a render pass");
        m_constants[slot] = static_cast<D3D12Buffer&>(buffer).gpuAddress() + offset;
        m_constantsDirty |= 1u << slot;
    }

    bool D3D12CommandList::flushBindings()
    {
        // Root descriptor tables point into a shader-visible heap, but views are created in CPU-only
        // heaps: CopyDescriptors may only read from those, since shader-visible heaps can live in
        // memory the CPU cannot read efficiently. Each change of bindings copies the bound views
        // into a fresh contiguous run of the frame's transient region, so a table the GPU may still
        // be reading is never overwritten.
        ID3D12Device* device = m_device->native();

        if (m_texturesDirty)
        {
            const D3D12Descriptor table = m_device->shaderVisibleSrvHeap().allocateTransient(m_transientRegion, kMaxTextureSlots);
            if (!table.valid())
                return false;
            std::array<UINT, kMaxTextureSlots> sourceSizes;
            sourceSizes.fill(1);
            const UINT tableSize = kMaxTextureSlots;
            device->CopyDescriptors(1, &table.cpu, &tableSize, kMaxTextureSlots, m_textures.data(), sourceSizes.data(),
                                    D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
            m_list->SetGraphicsRootDescriptorTable(kRootTextureTable, table.gpu);
            m_texturesDirty = false;
        }

        if (m_samplersDirty)
        {
            const D3D12Descriptor table =
                m_device->shaderVisibleSamplerHeap().allocateTransient(m_transientRegion, kMaxSamplerSlots);
            if (!table.valid())
                return false;
            std::array<UINT, kMaxSamplerSlots> sourceSizes;
            sourceSizes.fill(1);
            const UINT tableSize = kMaxSamplerSlots;
            device->CopyDescriptors(1, &table.cpu, &tableSize, kMaxSamplerSlots, m_samplers.data(), sourceSizes.data(),
                                    D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER);
            m_list->SetGraphicsRootDescriptorTable(kRootSamplerTable, table.gpu);
            m_samplersDirty = false;
        }

        // Root CBVs hold the GPU address directly, with no descriptor: the cheapest way to bind
        // small data that changes every draw.
        for (uint32_t slot = 0; slot < kMaxConstantSlots; ++slot)
            if (m_constantsDirty & (1u << slot))
                m_list->SetGraphicsRootConstantBufferView(kRootFirstConstants + slot, m_constants[slot]);
        m_constantsDirty = 0;
        return true;
    }

    void D3D12CommandList::draw(uint32_t vertexCount, uint32_t instanceCount, uint32_t firstVertex, uint32_t firstInstance)
    {
        ARC_ASSERT(m_pipeline, "Draw without a pipeline");
        if (flushBindings())
            m_list->DrawInstanced(vertexCount, instanceCount, firstVertex, firstInstance);
    }

    void D3D12CommandList::drawIndexed(uint32_t indexCount, uint32_t instanceCount, uint32_t firstIndex,
                                       int32_t baseVertex, uint32_t firstInstance)
    {
        ARC_ASSERT(m_pipeline, "Draw without a pipeline");
        if (flushBindings())
            m_list->DrawIndexedInstanced(indexCount, instanceCount, firstIndex, baseVertex, firstInstance);
    }

    void D3D12CommandList::copyBuffer(Buffer& dst, uint64_t dstOffset, Buffer& src, uint64_t srcOffset, uint64_t size)
    {
        ARC_ASSERT(!m_inRenderPass, "Copies are not allowed inside a render pass");
        ARC_ASSERT(dst.state() == ResourceState::CopyDest, "Copy destination '{}' is in state {}",
                   dst.desc().debugName, resourceStateName(dst.state()));
        ARC_ASSERT(src.state() == ResourceState::CopySource, "Copy source '{}' is in state {}",
                   src.desc().debugName, resourceStateName(src.state()));
        ARC_ASSERT(dstOffset + size <= dst.desc().size && srcOffset + size <= src.desc().size);

        m_list->CopyBufferRegion(static_cast<D3D12Buffer&>(dst).resource(), dstOffset,
                                 static_cast<D3D12Buffer&>(src).resource(), srcOffset, size);
    }

    void D3D12CommandList::copyBufferToTexture(Texture& dst, uint32_t mipLevel, Buffer& src, uint64_t srcOffset,
                                               uint32_t srcRowPitch)
    {
        ARC_ASSERT(!m_inRenderPass, "Copies are not allowed inside a render pass");
        validateTextureCopy(dst, mipLevel, ResourceState::CopyDest, src, srcOffset, srcRowPitch, ResourceState::CopySource);

        const Extent2D size = mipSize(dst.desc().size, mipLevel);
        D3D12_TEXTURE_COPY_LOCATION destination{};
        destination.pResource = static_cast<D3D12Texture&>(dst).resource();
        destination.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        destination.SubresourceIndex = mipLevel;

        D3D12_TEXTURE_COPY_LOCATION source{};
        source.pResource = static_cast<D3D12Buffer&>(src).resource();
        source.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
        source.PlacedFootprint.Offset = srcOffset;
        source.PlacedFootprint.Footprint = {toDxgiFormat(dst.desc().format), size.width, size.height, 1, srcRowPitch};

        m_list->CopyTextureRegion(&destination, 0, 0, 0, &source, nullptr);
    }

    void D3D12CommandList::copyTextureToBuffer(Buffer& dst, uint64_t dstOffset, uint32_t dstRowPitch, Texture& src,
                                               uint32_t mipLevel)
    {
        ARC_ASSERT(!m_inRenderPass, "Copies are not allowed inside a render pass");
        validateTextureCopy(src, mipLevel, ResourceState::CopySource, dst, dstOffset, dstRowPitch, ResourceState::CopyDest);

        const Extent2D size = mipSize(src.desc().size, mipLevel);
        D3D12_TEXTURE_COPY_LOCATION destination{};
        destination.pResource = static_cast<D3D12Buffer&>(dst).resource();
        destination.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
        destination.PlacedFootprint.Offset = dstOffset;
        destination.PlacedFootprint.Footprint = {toDxgiFormat(src.desc().format), size.width, size.height, 1, dstRowPitch};

        D3D12_TEXTURE_COPY_LOCATION source{};
        source.pResource = static_cast<D3D12Texture&>(src).resource();
        source.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        source.SubresourceIndex = mipLevel;

        m_list->CopyTextureRegion(&destination, 0, 0, 0, &source, nullptr);
    }

    void D3D12CommandList::barrier(Buffer& buffer, ResourceState state)
    {
        ARC_ASSERT(!m_inRenderPass, "Barriers are not allowed inside a render pass");
        // Upload heap resources must stay in GENERIC_READ and readback heap resources in
        // COPY_DEST for their whole lifetime; the debug layer rejects any transition.
        ARC_ASSERT(buffer.desc().memory == MemoryType::GpuOnly, "'{}' has a fixed state", buffer.desc().debugName);

        const D3D12_RESOURCE_STATES before = toD3D12State(buffer.state());
        const D3D12_RESOURCE_STATES after = toD3D12State(state);
        trackBufferState(buffer, state);
        if (before == after)
            return;

        D3D12_RESOURCE_BARRIER barrier{};
        barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        barrier.Transition.pResource = static_cast<D3D12Buffer&>(buffer).resource();
        barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        barrier.Transition.StateBefore = before;
        barrier.Transition.StateAfter = after;
        m_list->ResourceBarrier(1, &barrier);
    }

    void D3D12CommandList::barrier(Texture& texture, ResourceState state)
    {
        ARC_ASSERT(!m_inRenderPass, "Barriers are not allowed inside a render pass");
        if (texture.state() == state)
            return;

        D3D12_RESOURCE_BARRIER barrier{};
        barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        barrier.Transition.pResource = static_cast<D3D12Texture&>(texture).resource();
        barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        barrier.Transition.StateBefore = toD3D12State(texture.state());
        barrier.Transition.StateAfter = toD3D12State(state);
        m_list->ResourceBarrier(1, &barrier);

        texture.setState(state);
    }

    void D3D12CommandList::beginRenderPass(const RenderPassDesc& desc)
    {
        ARC_ASSERT(!m_inRenderPass, "Render passes do not nest");
        validateRenderPass(desc);

        auto& target = static_cast<D3D12Texture&>(*desc.color.texture);
        auto* depth = static_cast<D3D12Texture*>(desc.depth.texture);
        barrier(target, ResourceState::RenderTarget);
        if (depth)
            barrier(*depth, ResourceState::DepthWrite);
        m_inRenderPass = true;

        m_passHasDebugGroup = !desc.debugName.empty();
        if (m_passHasDebugGroup)
            beginEvent(m_list.Get(), desc.debugName);

        m_list->OMSetRenderTargets(1, &target.rtv, FALSE, depth ? &depth->dsv : nullptr);

        // D3D12 has no load or store actions: Clear is an explicit clear, and Load and DontCare
        // both leave the contents as they are. Store is likewise implicit.
        if (desc.color.load == LoadOp::Clear)
        {
            const ClearColor& c = desc.color.clearColor;
            const float color[] = {c.r, c.g, c.b, c.a};
            m_list->ClearRenderTargetView(target.rtv, color, 0, nullptr);
        }
        if (depth && desc.depth.load == LoadOp::Clear)
            m_list->ClearDepthStencilView(depth->dsv, D3D12_CLEAR_FLAG_DEPTH, desc.depth.clearDepth, 0, 0, nullptr);

        // Metal resets the viewport and scissor to the full target at the start of every pass;
        // doing the same here keeps the two backends consistent.
        const Extent2D size = target.desc().size;
        const D3D12_VIEWPORT viewport{0.0f, 0.0f, static_cast<float>(size.width), static_cast<float>(size.height),
                                      0.0f, 1.0f};
        const D3D12_RECT scissor{0, 0, static_cast<LONG>(size.width), static_cast<LONG>(size.height)};
        m_list->RSSetViewports(1, &viewport);
        m_list->RSSetScissorRects(1, &scissor);

        resetBindings();
    }

    void D3D12CommandList::endRenderPass()
    {
        ARC_ASSERT(m_inRenderPass);
        m_inRenderPass = false;
        m_pipeline = nullptr;

        if (m_passHasDebugGroup)
            m_list->EndEvent();
        m_passHasDebugGroup = false;
    }

    void D3D12CommandList::pushDebugGroup(std::string_view name)
    {
        beginEvent(m_list.Get(), name);
        ++m_debugDepth;
    }

    void D3D12CommandList::popDebugGroup()
    {
        ARC_ASSERT(m_debugDepth > 0, "Unbalanced popDebugGroup");
        m_list->EndEvent();
        --m_debugDepth;
    }
}

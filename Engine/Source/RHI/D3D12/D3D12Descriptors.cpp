#include "RHI/D3D12/D3D12Backend.h"

#include "Core/Assert.h"
#include "Core/Log.h"

#include <optional>

namespace arc::rhi::d3d12
{
    bool D3D12DescriptorHeap::init(ID3D12Device* device, const Desc& desc)
    {
        m_name = desc.debugName;
        m_persistentCount = desc.persistentCount;
        m_transientCountPerRegion = desc.transientCountPerRegion;
        m_transientUsed.assign(desc.transientRegions, 0);

        D3D12_DESCRIPTOR_HEAP_DESC heapDesc{};
        heapDesc.Type = desc.type;
        heapDesc.NumDescriptors = desc.persistentCount + desc.transientCountPerRegion * desc.transientRegions;
        heapDesc.Flags = desc.shaderVisible ? D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE : D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
        if (!succeeded(device->CreateDescriptorHeap(&heapDesc, IID_PPV_ARGS(&m_heap)), "CreateDescriptorHeap"))
            return false;
        setDebugName(m_heap.Get(), desc.debugName);

        m_stride = device->GetDescriptorHandleIncrementSize(desc.type);
        m_cpuStart = m_heap->GetCPUDescriptorHandleForHeapStart();
        if (desc.shaderVisible)
            m_gpuStart = m_heap->GetGPUDescriptorHandleForHeapStart();

        m_persistentSlots = HandleAllocator<D3D12Descriptor>(desc.persistentCount);
        return true;
    }

    void D3D12DescriptorHeap::release()
    {
        m_heap.Reset();
        m_persistentSlots = HandleAllocator<D3D12Descriptor>(0);
        m_transientUsed.clear();
    }

    D3D12Descriptor D3D12DescriptorHeap::at(uint32_t index) const
    {
        D3D12Descriptor descriptor;
        descriptor.cpu.ptr = m_cpuStart.ptr + SIZE_T{index} * m_stride;
        if (m_gpuStart.ptr != 0)
            descriptor.gpu.ptr = m_gpuStart.ptr + UINT64{index} * m_stride;
        return descriptor;
    }

    D3D12Descriptor D3D12DescriptorHeap::allocate()
    {
        const std::optional<Handle<D3D12Descriptor>> handle = m_persistentSlots.allocate();
        if (!handle)
        {
            ARC_LOG_ERROR("Descriptor heap '{}' is full ({} persistent descriptors)", m_name, m_persistentCount);
            return {};
        }
        D3D12Descriptor descriptor = at(handle->index);
        descriptor.handle = *handle;
        return descriptor;
    }

    void D3D12DescriptorHeap::free(const D3D12Descriptor& descriptor)
    {
        if (!descriptor.valid())
            return;
        // Descriptors live in CPU-only heaps (copied into tables while recording) or are owned by
        // ImGui, which waits for its own frames, so the slot can be reused straight away.
        const bool freed = m_persistentSlots.free(descriptor.handle);
        ARC_ASSERT(freed, "Descriptor {} of '{}' was already freed", descriptor.handle.index, m_name);
    }

    void D3D12DescriptorHeap::free(D3D12_CPU_DESCRIPTOR_HANDLE cpu)
    {
        if (cpu.ptr == 0)
            return;
        ARC_ASSERT(cpu.ptr >= m_cpuStart.ptr && (cpu.ptr - m_cpuStart.ptr) / m_stride < m_persistentCount,
                   "Descriptor does not belong to the persistent part of '{}'", m_name);
        const auto index = static_cast<uint32_t>((cpu.ptr - m_cpuStart.ptr) / m_stride);
        const Handle<D3D12Descriptor> handle = m_persistentSlots.handleAt(index);
        ARC_ASSERT(!handle.isNull(), "Descriptor {} of '{}' was already freed", index, m_name);
        m_persistentSlots.free(handle);
    }

    void D3D12DescriptorHeap::resetTransient(uint32_t region)
    {
        ARC_ASSERT(region < m_transientUsed.size());
        m_transientUsed[region] = 0;
    }

    D3D12Descriptor D3D12DescriptorHeap::allocateTransient(uint32_t region, uint32_t count)
    {
        ARC_ASSERT(region < m_transientUsed.size());
        uint32_t& used = m_transientUsed[region];
        if (used + count > m_transientCountPerRegion)
        {
            ARC_LOG_ERROR("Descriptor heap '{}' ran out of transient descriptors this frame ({} per frame); "
                          "the draw is skipped",
                          m_name, m_transientCountPerRegion);
            return {};
        }
        const uint32_t index = m_persistentCount + region * m_transientCountPerRegion + used;
        used += count;
        return at(index);
    }

    D3D12Texture::D3D12Texture(D3D12Device& device, ComPtr<ID3D12Resource> resource, const TextureDesc& desc,
                               const D3D12Descriptor& srvDescriptor, const D3D12Descriptor& rtvDescriptor,
                               const D3D12Descriptor& dsvDescriptor)
        : srv(srvDescriptor.cpu)
        , rtv(rtvDescriptor.cpu)
        , dsv(dsvDescriptor.cpu)
        , m_resource(std::move(resource))
        , m_device(&device)
        , m_srvDescriptor(srvDescriptor)
        , m_rtvDescriptor(rtvDescriptor)
        , m_dsvDescriptor(dsvDescriptor)
    {
        m_desc = desc;
    }

    D3D12Texture::~D3D12Texture()
    {
        if (!m_device)
            return;
        // The views are CPU-only descriptors: draws copied the SRV into a shader-visible table and
        // OMSetRenderTargets read the RTV and DSV when they were recorded, so the slots can be reused
        // now. The resource itself may still be in use by the GPU.
        m_device->cpuSrvHeap().free(m_srvDescriptor);
        m_device->rtvHeap().free(m_rtvDescriptor);
        m_device->dsvHeap().free(m_dsvDescriptor);
        m_device->retire(std::move(m_resource));
    }

    void D3D12Texture::reset(ComPtr<ID3D12Resource> resource, const TextureDesc& desc, ResourceState initialState)
    {
        m_resource = std::move(resource);
        m_desc = desc;
        m_state = initialState;
    }

    void D3D12Texture::release()
    {
        m_resource.Reset();
        m_state = ResourceState::Common;
    }

    D3D12Sampler::D3D12Sampler(D3D12Device& device, const SamplerDesc& desc, const D3D12Descriptor& descriptor)
        : Sampler(desc)
        , m_device(device)
        , m_descriptor(descriptor)
    {
    }

    D3D12Sampler::~D3D12Sampler()
    {
        // A sampler is only a CPU-only descriptor, already copied into the tables that use it.
        m_device.cpuSamplerHeap().free(m_descriptor);
    }

    D3D12Buffer::~D3D12Buffer()
    {
        m_device.retire(std::move(m_resource));
    }

    D3D12Pipeline::~D3D12Pipeline()
    {
        m_device.retire(std::move(m_state));
    }
}

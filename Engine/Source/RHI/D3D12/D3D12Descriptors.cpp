#include "RHI/D3D12/D3D12Backend.h"

#include "Core/Assert.h"
#include "Core/Log.h"

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

        // Reversed, so the lowest indices are handed out first and the heap reads in order in PIX.
        m_freeList.resize(desc.persistentCount);
        for (uint32_t i = 0; i < desc.persistentCount; ++i)
            m_freeList[i] = desc.persistentCount - 1 - i;
        return true;
    }

    void D3D12DescriptorHeap::release()
    {
        m_heap.Reset();
        m_freeList.clear();
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
        if (m_freeList.empty())
        {
            ARC_LOG_ERROR("Descriptor heap '{}' is full ({} persistent descriptors)", m_name, m_persistentCount);
            return {};
        }
        const uint32_t index = m_freeList.back();
        m_freeList.pop_back();
        return at(index);
    }

    void D3D12DescriptorHeap::free(D3D12_CPU_DESCRIPTOR_HANDLE cpu)
    {
        if (cpu.ptr == 0)
            return;
        ARC_ASSERT(cpu.ptr >= m_cpuStart.ptr && (cpu.ptr - m_cpuStart.ptr) / m_stride < m_persistentCount,
                   "Descriptor does not belong to the persistent part of '{}'", m_name);
        m_freeList.push_back(static_cast<uint32_t>((cpu.ptr - m_cpuStart.ptr) / m_stride));
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
                               D3D12_CPU_DESCRIPTOR_HANDLE srvHandle, D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle)
        : srv(srvHandle)
        , rtv(rtvHandle)
        , m_resource(std::move(resource))
        , m_device(&device)
    {
        m_desc = desc;
    }

    D3D12Texture::~D3D12Texture()
    {
        // The views are plain CPU descriptors; draws that used them copied them into shader-visible
        // tables while recording, so they can be reused straight away. The resource itself must not
        // be in use by the GPU, the same rule as for buffers.
        if (m_device)
        {
            m_device->cpuSrvHeap().free(srv);
            m_device->rtvHeap().free(rtv);
        }
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

    D3D12Sampler::D3D12Sampler(D3D12Device& device, const SamplerDesc& desc, D3D12_CPU_DESCRIPTOR_HANDLE descriptor)
        : Sampler(desc)
        , m_device(device)
        , m_descriptor(descriptor)
    {
    }

    D3D12Sampler::~D3D12Sampler()
    {
        m_device.cpuSamplerHeap().free(m_descriptor);
    }
}

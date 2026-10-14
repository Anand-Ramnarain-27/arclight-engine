#include "RHI/Device.h"

#include "RHI/Backends.h"

namespace arc::rhi
{
    const char* backendName(Backend backend)
    {
        switch (backend)
        {
            case Backend::D3D12: return "Direct3D 12";
            case Backend::Metal: return "Metal";
            case Backend::Null:  return "Null";
        }
        return "Unknown";
    }

    const char* resourceStateName(ResourceState state)
    {
        switch (state)
        {
            case ResourceState::Common:         return "Common";
            case ResourceState::CopySource:     return "CopySource";
            case ResourceState::CopyDest:       return "CopyDest";
            case ResourceState::VertexBuffer:   return "VertexBuffer";
            case ResourceState::IndexBuffer:    return "IndexBuffer";
            case ResourceState::ConstantBuffer: return "ConstantBuffer";
            case ResourceState::ShaderRead:     return "ShaderRead";
            case ResourceState::RenderTarget:   return "RenderTarget";
            case ResourceState::Present:        return "Present";
        }
        return "Unknown";
    }

    const char* shaderStageName(ShaderStage stage)
    {
        switch (stage)
        {
            case ShaderStage::Vertex: return "vertex";
            case ShaderStage::Pixel:  return "pixel";
        }
        return "unknown";
    }

    std::unique_ptr<Device> Device::create(const DeviceDesc& desc)
    {
#if ARC_PLATFORM_WINDOWS
        return createD3D12Device(desc);
#elif ARC_PLATFORM_MACOS
        return createMetalDevice(desc);
#else
        return createNullDevice(desc);
#endif
    }
}

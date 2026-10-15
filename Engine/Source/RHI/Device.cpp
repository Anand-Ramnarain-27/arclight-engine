#include "RHI/Device.h"

#include "Core/Assert.h"
#include "RHI/Backends.h"

#include <algorithm>
#include <bit>

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

    uint32_t bytesPerPixel(Format format)
    {
        switch (format)
        {
            case Format::Unknown:          return 0;
            case Format::RGBA8_UNORM:
            case Format::RGBA8_UNORM_SRGB:
            case Format::BGRA8_UNORM:      return 4;
        }
        return 0;
    }

    uint32_t fullMipCount(Extent2D size)
    {
        return static_cast<uint32_t>(std::bit_width(std::max({size.width, size.height, 1u})));
    }

    Extent2D mipSize(Extent2D size, uint32_t mipLevel)
    {
        return {std::max(size.width >> mipLevel, 1u), std::max(size.height >> mipLevel, 1u)};
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

    void Device::validateTextureDesc(const TextureDesc& desc)
    {
        ARC_ASSERT(desc.size.width > 0 && desc.size.height > 0, "Texture '{}' has zero size", desc.debugName);
        ARC_ASSERT(desc.format != Format::Unknown, "Texture '{}' has no format", desc.debugName);
        ARC_ASSERT(desc.mipLevels >= 1 && desc.mipLevels <= fullMipCount(desc.size),
                   "Texture '{}' asks for {} mip levels; its size allows 1 to {}", desc.debugName, desc.mipLevels,
                   fullMipCount(desc.size));
        ARC_ASSERT(desc.usage != TextureUsage::None, "Texture '{}' has no usage", desc.debugName);
    }

    void Device::validateSamplerDesc(const SamplerDesc& desc)
    {
        ARC_ASSERT(desc.maxAnisotropy >= 1 && desc.maxAnisotropy <= 16, "Sampler '{}': maxAnisotropy {} is outside [1, 16]",
                   desc.debugName, desc.maxAnisotropy);
        ARC_ASSERT(desc.minLod <= desc.maxLod, "Sampler '{}': minLod is above maxLod", desc.debugName);
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

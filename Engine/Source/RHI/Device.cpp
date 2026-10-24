#include "RHI/Device.h"

#include "Core/Assert.h"
#include "Core/Log.h"
#include "RHI/Backends.h"
#include "RHI/Buffer.h"
#include "RHI/Sampler.h"
#include "RHI/Texture.h"

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
            case Format::BGRA8_UNORM:
            case Format::D32_FLOAT:        return 4;
            case Format::RGBA16_FLOAT:     return 8;
        }
        return 0;
    }

    bool isDepthFormat(Format format)
    {
        return format == Format::D32_FLOAT;
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
            case ResourceState::DepthWrite:     return "DepthWrite";
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

    namespace
    {
        // The rules shared by every backend, checked before the backend sees the description.
        void validateBufferDesc(const BufferDesc& desc)
        {
            ARC_ASSERT(desc.size > 0, "Buffer '{}' has zero size", desc.debugName);
            if (hasFlag(desc.usage, BufferUsage::Structured))
            {
                ARC_ASSERT(desc.stride > 0 && desc.stride % 4 == 0 && desc.size % desc.stride == 0,
                           "Structured buffer '{}': stride {} must be a non-zero multiple of 4 that divides the size {}",
                           desc.debugName, desc.stride, desc.size);
                ARC_ASSERT(desc.memory != MemoryType::Readback, "Structured buffer '{}' cannot be in readback memory",
                           desc.debugName);
            }
        }

        void validateTextureDesc(const TextureDesc& desc)
        {
            ARC_ASSERT(desc.size.width > 0 && desc.size.height > 0, "Texture '{}' has zero size", desc.debugName);
            ARC_ASSERT(desc.format != Format::Unknown, "Texture '{}' has no format", desc.debugName);
            ARC_ASSERT(desc.mipLevels >= 1 && desc.mipLevels <= fullMipCount(desc.size),
                       "Texture '{}' asks for {} mip levels; its size allows 1 to {}", desc.debugName,
                       desc.mipLevels, fullMipCount(desc.size));
            ARC_ASSERT(desc.usage != TextureUsage::None, "Texture '{}' has no usage", desc.debugName);
            ARC_ASSERT(desc.type != TextureType::Cube ||
                           (desc.size.width == desc.size.height && desc.usage == TextureUsage::Sampled),
                       "Cube texture '{}' must be square and only sampled", desc.debugName);
            const bool depthUsage = hasFlag(desc.usage, TextureUsage::DepthStencil);
            ARC_ASSERT(depthUsage == isDepthFormat(desc.format),
                       "Texture '{}': DepthStencil usage and a depth format go together", desc.debugName);
            ARC_ASSERT(!depthUsage || (desc.usage == TextureUsage::DepthStencil && desc.mipLevels == 1),
                       "Texture '{}': a depth target has a single mip level and no other usage", desc.debugName);
        }

        void validateSamplerDesc(const SamplerDesc& desc)
        {
            ARC_ASSERT(desc.maxAnisotropy >= 1 && desc.maxAnisotropy <= 16,
                       "Sampler '{}': maxAnisotropy {} is outside [1, 16]", desc.debugName, desc.maxAnisotropy);
            ARC_ASSERT(desc.minLod <= desc.maxLod, "Sampler '{}': minLod is above maxLod", desc.debugName);
        }

        template <typename Resource, typename Pool>
        void reportLeaks(Pool& pool, const char* kind)
        {
            pool.forEach([kind](auto /*handle*/, Resource* resource) {
                ARC_LOG_ERROR("Leaked {}: '{}' was not destroyed before its device", kind, resource->desc().debugName);
            });
        }
    }

    Device::~Device()
    {
        reportLeaks<Buffer>(m_buffers, "buffer");
        reportLeaks<Texture>(m_textures, "texture");
        reportLeaks<Sampler>(m_samplers, "sampler");

        // A leaked resource destroyed later must not reach into this device.
        m_buffers.forEach([](BufferHandle, Buffer* buffer) { buffer->m_owner = nullptr; });
        m_textures.forEach([](TextureHandle, Texture* texture) { texture->m_owner = nullptr; });
        m_samplers.forEach([](SamplerHandle, Sampler* sampler) { sampler->m_owner = nullptr; });
    }

    std::unique_ptr<Buffer> Device::createBuffer(const BufferDesc& desc)
    {
        validateBufferDesc(desc);
        std::unique_ptr<Buffer> buffer = createBufferImpl(desc);
        if (buffer)
        {
            buffer->m_owner = this;
            buffer->m_handle = m_buffers.insert(buffer.get());
        }
        return buffer;
    }

    std::unique_ptr<Texture> Device::createTexture(const TextureDesc& desc)
    {
        validateTextureDesc(desc);
        std::unique_ptr<Texture> texture = createTextureImpl(desc);
        if (texture)
        {
            texture->m_owner = this;
            texture->m_handle = m_textures.insert(texture.get());
        }
        return texture;
    }

    std::unique_ptr<Sampler> Device::createSampler(const SamplerDesc& desc)
    {
        validateSamplerDesc(desc);
        std::unique_ptr<Sampler> sampler = createSamplerImpl(desc);
        if (sampler)
        {
            sampler->m_owner = this;
            sampler->m_handle = m_samplers.insert(sampler.get());
        }
        return sampler;
    }

    Buffer* Device::find(BufferHandle handle) const
    {
        Buffer* const* buffer = m_buffers.get(handle);
        return buffer ? *buffer : nullptr;
    }

    Texture* Device::find(TextureHandle handle) const
    {
        Texture* const* texture = m_textures.get(handle);
        return texture ? *texture : nullptr;
    }

    Sampler* Device::find(SamplerHandle handle) const
    {
        Sampler* const* sampler = m_samplers.get(handle);
        return sampler ? *sampler : nullptr;
    }

    Device::ResourceCounts Device::resourceCounts() const
    {
        return {m_buffers.size(), m_textures.size(), m_samplers.size(), awaitingReleaseCount()};
    }

    // Each resource removes itself from its device's registry. The backend's destructor has
    // already run by now and has handed the GPU object to the deferred release queue if needed.
    Buffer::~Buffer()
    {
        if (m_owner)
            m_owner->m_buffers.remove(m_handle);
    }

    Texture::~Texture()
    {
        if (m_owner)
            m_owner->m_textures.remove(m_handle);
    }

    Sampler::~Sampler()
    {
        if (m_owner)
            m_owner->m_samplers.remove(m_handle);
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

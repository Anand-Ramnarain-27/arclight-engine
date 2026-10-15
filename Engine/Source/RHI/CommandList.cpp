#include "RHI/CommandList.h"

#include "Core/Assert.h"
#include "RHI/Buffer.h"
#include "RHI/Texture.h"
#include "RHI/UploadRing.h"

#include <algorithm>
#include <cstring>

namespace arc::rhi
{
    void CommandList::decayBufferStates()
    {
        for (Buffer* buffer : m_trackedBuffers)
            buffer->setState(ResourceState::Common);
        m_trackedBuffers.clear();
    }

    void CommandList::setConstants(uint32_t slot, std::span<const std::byte> data)
    {
        ARC_ASSERT(slot < kMaxConstantSlots, "Constant slot {} is out of range", slot);
        ARC_ASSERT(!data.empty() && data.size() <= kMaxConstantBufferSize, "Constant data of {} bytes", data.size());
        ARC_ASSERT(m_uploadRing, "The device did not give this command list an upload ring");

        const UploadAllocation allocation = m_uploadRing->allocate(m_uploadRegion, data.size(), kConstantBufferAlignment);
        if (!allocation)
            return;
        std::memcpy(allocation.cpu, data.data(), data.size());
        bindConstants(slot, *allocation.buffer, allocation.offset);
    }

    void CommandList::trackBufferState(Buffer& buffer, ResourceState state)
    {
        if (buffer.state() == ResourceState::Common &&
            std::find(m_trackedBuffers.begin(), m_trackedBuffers.end(), &buffer) == m_trackedBuffers.end())
            m_trackedBuffers.push_back(&buffer);
        buffer.setState(state);
    }

    void CommandList::validateTextureCopy(const Texture& texture, uint32_t mipLevel, ResourceState textureState,
                                          const Buffer& buffer, uint64_t bufferOffset, uint32_t rowPitch,
                                          ResourceState bufferState)
    {
        const TextureDesc& desc = texture.desc();
        ARC_ASSERT(mipLevel < desc.mipLevels, "'{}' has no mip level {}", desc.debugName, mipLevel);
        ARC_ASSERT(texture.state() == textureState, "Texture '{}' is in state {}, expected {}", desc.debugName,
                   resourceStateName(texture.state()), resourceStateName(textureState));
        ARC_ASSERT(buffer.state() == bufferState, "Buffer '{}' is in state {}, expected {}", buffer.desc().debugName,
                   resourceStateName(buffer.state()), resourceStateName(bufferState));
        ARC_ASSERT(bufferOffset % kTextureCopyOffsetAlignment == 0, "Texture copy offset {} is not a multiple of {}",
                   bufferOffset, kTextureCopyOffsetAlignment);
        ARC_ASSERT(rowPitch % kTextureCopyRowAlignment == 0, "Texture copy row pitch {} is not a multiple of {}",
                   rowPitch, kTextureCopyRowAlignment);

        const Extent2D size = mipSize(desc.size, mipLevel);
        ARC_ASSERT(rowPitch >= size.width * bytesPerPixel(desc.format), "Row pitch {} is smaller than a row of '{}'",
                   rowPitch, desc.debugName);
        ARC_ASSERT(bufferOffset + uint64_t{rowPitch} * size.height <= buffer.desc().size,
                   "Texture copy runs past the end of '{}'", buffer.desc().debugName);
    }

    void CommandList::validateTextureBinding(uint32_t slot, const Texture& texture)
    {
        ARC_ASSERT(slot < kMaxTextureSlots, "Texture slot {} is out of range", slot);
        ARC_ASSERT(hasFlag(texture.desc().usage, TextureUsage::Sampled), "'{}' was not created with Sampled usage",
                   texture.desc().debugName);
        ARC_ASSERT(texture.state() == ResourceState::ShaderRead, "Texture '{}' is in state {}, expected ShaderRead",
                   texture.desc().debugName, resourceStateName(texture.state()));
    }

    void CommandList::validateSamplerBinding(uint32_t slot)
    {
        ARC_ASSERT(slot < kMaxSamplerSlots, "Sampler slot {} is out of range", slot);
    }
}

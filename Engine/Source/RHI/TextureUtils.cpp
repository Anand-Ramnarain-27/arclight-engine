#include "RHI/TextureUtils.h"

#include "Core/Assert.h"
#include "Core/Log.h"
#include "RHI/Buffer.h"
#include "RHI/CommandList.h"
#include "RHI/Device.h"
#include "RHI/Texture.h"

#include <cstring>
#include <memory>

namespace arc::rhi
{
    namespace
    {
        constexpr uint64_t alignUp(uint64_t value, uint64_t alignment)
        {
            return (value + alignment - 1) / alignment * alignment;
        }
    }

    TextureCopyLayout computeTextureCopyLayout(const TextureDesc& desc)
    {
        TextureCopyLayout layout;
        layout.levels.reserve(uint64_t{desc.mipLevels} * arrayLayerCount(desc));

        uint64_t offset = 0;
        for (uint32_t layer = 0; layer < arrayLayerCount(desc); ++layer)
            for (uint32_t mip = 0; mip < desc.mipLevels; ++mip)
            {
                TextureCopyLayout::Level level;
                level.size = mipSize(desc.size, mip);
                level.rowBytes = level.size.width * bytesPerPixel(desc.format);
                level.rowPitch = static_cast<uint32_t>(alignUp(level.rowBytes, kTextureCopyRowAlignment));
                level.offset = alignUp(offset, kTextureCopyOffsetAlignment);
                offset = level.offset + uint64_t{level.rowPitch} * level.size.height;
                layout.levels.push_back(level);
            }
        layout.totalSize = offset;
        return layout;
    }

    bool uploadTexture(Device& device, Texture& texture, std::span<const std::span<const std::byte>> levels)
    {
        const TextureDesc& desc = texture.desc();
        const uint32_t subresources = desc.mipLevels * arrayLayerCount(desc);
        ARC_ASSERT(levels.size() == subresources, "'{}' has {} subresources but {} were given", desc.debugName,
                   subresources, levels.size());

        const TextureCopyLayout layout = computeTextureCopyLayout(desc);
        auto staging = device.createBuffer({
            .size = layout.totalSize,
            .memory = MemoryType::Upload,
            .debugName = "Staging (" + desc.debugName + ")",
        });
        if (!staging)
            return false;

        auto* bytes = static_cast<std::byte*>(staging->mappedData());
        for (uint32_t index = 0; index < subresources; ++index)
        {
            const TextureCopyLayout::Level& level = layout.levels[index];
            ARC_ASSERT(levels[index].size() == uint64_t{level.rowBytes} * level.size.height,
                       "Subresource {} of '{}' should be {} bytes, got {}", index, desc.debugName,
                       uint64_t{level.rowBytes} * level.size.height, levels[index].size());
            for (uint32_t row = 0; row < level.size.height; ++row)
                std::memcpy(bytes + level.offset + uint64_t{row} * level.rowPitch,
                            levels[index].data() + uint64_t{row} * level.rowBytes, level.rowBytes);
        }

        const ResourceState finalState =
            hasFlag(desc.usage, TextureUsage::Sampled) ? ResourceState::ShaderRead : ResourceState::Common;
        device.submitImmediate([&](CommandList& commandList) {
            commandList.barrier(texture, ResourceState::CopyDest);
            for (uint32_t layer = 0; layer < arrayLayerCount(desc); ++layer)
                for (uint32_t mip = 0; mip < desc.mipLevels; ++mip)
                {
                    const TextureCopyLayout::Level& level = layout.levels[subresourceIndex(desc, mip, layer)];
                    commandList.copyBufferToTexture(texture, mip, layer, *staging, level.offset, level.rowPitch);
                }
            commandList.barrier(texture, finalState);
        });

        // submitImmediate has waited for the GPU, so the staging buffer can be destroyed here.
        return true;
    }

    std::vector<std::byte> readbackTexture(Device& device, Texture& texture, uint32_t mipLevel, uint32_t arrayLayer)
    {
        const TextureDesc& desc = texture.desc();
        ARC_ASSERT(mipLevel < desc.mipLevels && arrayLayer < arrayLayerCount(desc));

        const TextureCopyLayout::Level level = computeTextureCopyLayout(desc).levels[subresourceIndex(desc, mipLevel, arrayLayer)];
        auto readback = device.createBuffer({
            .size = uint64_t{level.rowPitch} * level.size.height,
            .memory = MemoryType::Readback,
            .debugName = "Readback (" + desc.debugName + ")",
        });
        if (!readback)
            return {};

        const ResourceState previousState = texture.state();
        device.submitImmediate([&](CommandList& commandList) {
            commandList.barrier(texture, ResourceState::CopySource);
            commandList.copyTextureToBuffer(*readback, 0, level.rowPitch, texture, mipLevel, arrayLayer);
            commandList.barrier(texture, previousState);
        });

        std::vector<std::byte> bytes(uint64_t{level.rowBytes} * level.size.height);
        const auto* source = static_cast<const std::byte*>(readback->mappedData());
        for (uint32_t row = 0; row < level.size.height; ++row)
            std::memcpy(bytes.data() + uint64_t{row} * level.rowBytes, source + uint64_t{row} * level.rowPitch,
                        level.rowBytes);
        return bytes;
    }

    bool verifyTextureRoundTrip(Device& device)
    {
        // 37 x 19: neither dimension is a power of two and no row is a multiple of 256 bytes, so
        // every level needs row padding and the mip sizes round down.
        const TextureDesc desc{
            .size = {37, 19},
            .format = Format::RGBA8_UNORM,
            .mipLevels = 3,
            .usage = TextureUsage::Sampled,
            .debugName = "Round-trip test texture",
        };
        auto texture = device.createTexture(desc);
        if (!texture)
            return false;

        std::vector<std::vector<std::byte>> pattern(desc.mipLevels);
        std::vector<std::span<const std::byte>> levels;
        for (uint32_t mip = 0; mip < desc.mipLevels; ++mip)
        {
            const Extent2D size = mipSize(desc.size, mip);
            pattern[mip].resize(uint64_t{size.width} * size.height * 4);
            for (size_t i = 0; i < pattern[mip].size(); ++i)
                pattern[mip][i] = static_cast<std::byte>((i * 7 + mip * 61 + (i >> 8)) & 0xFF);
            levels.emplace_back(pattern[mip]);
        }

        if (!uploadTexture(device, *texture, levels))
            return false;

        for (uint32_t mip = 0; mip < desc.mipLevels; ++mip)
        {
            if (readbackTexture(device, *texture, mip) != pattern[mip])
            {
                ARC_LOG_ERROR("Texture round-trip: mip level {} read back did not match what was uploaded", mip);
                return false;
            }
        }
        return true;
    }
}

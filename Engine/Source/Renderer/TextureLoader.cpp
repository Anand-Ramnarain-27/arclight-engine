#include "Renderer/TextureLoader.h"

#include "Core/Assert.h"
#include "Core/Log.h"
#include "RHI/Device.h"
#include "RHI/Texture.h"
#include "RHI/TextureUtils.h"

#include <cstddef>
#include <vector>

namespace arc
{
    std::unique_ptr<rhi::Texture> createTexture(rhi::Device& device, std::span<const Image> levels, ColorSpace colorSpace,
                                                std::string debugName)
    {
        ARC_ASSERT(!levels.empty() && !levels[0].empty(), "Texture '{}' has no pixels", debugName);
        for (size_t i = 1; i < levels.size(); ++i)
        {
            const Extent2D expected = rhi::mipSize(levels[0].size, static_cast<uint32_t>(i));
            ARC_ASSERT(levels[i].size.width == expected.width && levels[i].size.height == expected.height,
                       "Mip level {} of '{}' is {}x{}, expected {}x{}", i, debugName, levels[i].size.width,
                       levels[i].size.height, expected.width, expected.height);
        }

        auto texture = device.createTexture({
            .size = levels[0].size,
            .format = colorSpace == ColorSpace::Srgb ? rhi::Format::RGBA8_UNORM_SRGB : rhi::Format::RGBA8_UNORM,
            .mipLevels = static_cast<uint32_t>(levels.size()),
            .usage = rhi::TextureUsage::Sampled,
            .debugName = std::move(debugName),
        });
        if (!texture)
            return nullptr;

        std::vector<std::span<const std::byte>> data;
        data.reserve(levels.size());
        for (const Image& level : levels)
            data.emplace_back(level.pixels);

        if (!rhi::uploadTexture(device, *texture, data))
            return nullptr;
        return texture;
    }

    std::unique_ptr<rhi::Texture> loadTexture(rhi::Device& device, const std::string& path, ColorSpace colorSpace)
    {
        Image image = loadImage(path);
        if (image.empty())
            return nullptr;

        const Extent2D size = image.size;
        const std::vector<Image> levels = generateMipChain(std::move(image), colorSpace);
        auto texture = createTexture(device, levels, colorSpace, path);
        if (texture)
            ARC_LOG_INFO("Loaded texture '{}' ({}x{}, {} mip levels)", path, size.width, size.height, levels.size());
        return texture;
    }
}

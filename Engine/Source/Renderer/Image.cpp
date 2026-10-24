#include "Renderer/Image.h"

#include "Core/Assert.h"
#include "Core/Log.h"
#include "Math/Color.h"
#include "Platform/FileSystem.h"
#include "RHI/RHITypes.h"

#include <stb_image.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <optional>

namespace arc
{
    namespace
    {
        const std::array<float, 256>& srgbDecodeTable()
        {
            static const std::array<float, 256> table = [] {
                std::array<float, 256> values{};
                for (size_t i = 0; i < values.size(); ++i)
                    values[i] = srgbToLinear(static_cast<float>(i) / 255.0f);
                return values;
            }();
            return table;
        }

        std::byte toByte(float normalized)
        {
            return static_cast<std::byte>(std::lround(std::clamp(normalized, 0.0f, 1.0f) * 255.0f));
        }
    }

    Image loadImage(const std::string& path)
    {
        const std::optional<std::vector<std::byte>> data = readFile(path);
        if (!data)
        {
            ARC_LOG_ERROR("Cannot read image '{}'", path);
            return {};
        }
        return decodeImage(*data, path);
    }

    Image decodeImage(std::span<const std::byte> encoded, std::string_view debugName)
    {
        int width = 0;
        int height = 0;
        int channelsInFile = 0;
        // Forcing four channels gives one pixel layout for every file: grey, grey+alpha and RGB
        // images are expanded, with alpha 255 where the file has none.
        stbi_uc* pixels = stbi_load_from_memory(reinterpret_cast<const stbi_uc*>(encoded.data()),
                                                static_cast<int>(encoded.size()), &width, &height, &channelsInFile, 4);
        if (!pixels)
        {
            ARC_LOG_ERROR("Cannot decode image '{}': {}", debugName, stbi_failure_reason());
            return {};
        }

        Image image;
        image.size = {static_cast<uint32_t>(width), static_cast<uint32_t>(height)};
        image.pixels.resize(size_t{image.size.width} * image.size.height * 4);
        std::memcpy(image.pixels.data(), pixels, image.pixels.size());
        stbi_image_free(pixels);
        return image;
    }

    Image downsample(const Image& image, ColorSpace colorSpace)
    {
        ARC_ASSERT(image.pixels.size() == size_t{image.size.width} * image.size.height * 4);

        const Extent2D source = image.size;
        Image result;
        result.size = rhi::mipSize(source, 1);
        result.pixels.resize(size_t{result.size.width} * result.size.height * 4);

        const std::array<float, 256>& decode = srgbDecodeTable();
        const auto channel = [&](uint32_t x, uint32_t y, uint32_t c) {
            const auto value = std::to_integer<uint8_t>(image.pixels[(size_t{y} * source.width + x) * 4 + c]);
            return (colorSpace == ColorSpace::Srgb && c < 3) ? decode[value] : static_cast<float>(value) / 255.0f;
        };

        for (uint32_t y = 0; y < result.size.height; ++y)
        {
            // An odd row or column count leaves the last source row or column out of the box; a
            // 1-pixel dimension reuses its only row or column.
            const uint32_t y0 = std::min(2 * y, source.height - 1);
            const uint32_t y1 = std::min(2 * y + 1, source.height - 1);
            for (uint32_t x = 0; x < result.size.width; ++x)
            {
                const uint32_t x0 = std::min(2 * x, source.width - 1);
                const uint32_t x1 = std::min(2 * x + 1, source.width - 1);
                for (uint32_t c = 0; c < 4; ++c)
                {
                    float average = 0.25f * (channel(x0, y0, c) + channel(x1, y0, c) + channel(x0, y1, c) + channel(x1, y1, c));
                    if (colorSpace == ColorSpace::Srgb && c < 3)
                        average = linearToSrgb(average);
                    result.pixels[(size_t{y} * result.size.width + x) * 4 + c] = toByte(average);
                }
            }
        }
        return result;
    }

    std::vector<Image> generateMipChain(Image base, ColorSpace colorSpace)
    {
        const uint32_t levels = rhi::fullMipCount(base.size);
        std::vector<Image> chain;
        chain.reserve(levels);
        chain.push_back(std::move(base));
        for (uint32_t level = 1; level < levels; ++level)
            chain.push_back(downsample(chain.back(), colorSpace));
        return chain;
    }
}

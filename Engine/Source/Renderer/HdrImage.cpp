#include "Renderer/HdrImage.h"

#include "Core/Assert.h"
#include "Core/Log.h"
#include "Platform/FileSystem.h"
#include "RHI/RHITypes.h"

#include <stb_image.h>

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstring>
#include <optional>

namespace arc
{
    HdrImage decodeHdrImage(std::span<const std::byte> encoded, std::string_view debugName)
    {
        const auto* bytes = reinterpret_cast<const stbi_uc*>(encoded.data());
        const int size = static_cast<int>(encoded.size());
        if (!stbi_is_hdr_from_memory(bytes, size))
        {
            ARC_LOG_ERROR("'{}' is not a Radiance .hdr image", debugName);
            return {};
        }
        int width = 0;
        int height = 0;
        int channelsInFile = 0;
        float* pixels = stbi_loadf_from_memory(bytes, size, &width, &height, &channelsInFile, 4);
        if (!pixels)
        {
            ARC_LOG_ERROR("Cannot decode HDR image '{}': {}", debugName, stbi_failure_reason());
            return {};
        }
        HdrImage image;
        image.size = {static_cast<uint32_t>(width), static_cast<uint32_t>(height)};
        image.pixels.assign(pixels, pixels + size_t{image.size.width} * image.size.height * 4);
        stbi_image_free(pixels);
        return image;
    }

    HdrImage loadHdrImage(const std::string& path)
    {
        const std::optional<std::vector<std::byte>> data = readFile(path);
        if (!data)
        {
            ARC_LOG_ERROR("Cannot read image '{}'", path);
            return {};
        }
        return decodeHdrImage(*data, path);
    }

    HdrImage downsample(const HdrImage& image)
    {
        ARC_ASSERT(image.pixels.size() == size_t{image.size.width} * image.size.height * 4);
        const Extent2D source = image.size;
        HdrImage result;
        result.size = rhi::mipSize(source, 1);
        result.pixels.resize(size_t{result.size.width} * result.size.height * 4);
        for (uint32_t y = 0; y < result.size.height; ++y)
        {
            const uint32_t y0 = std::min(2 * y, source.height - 1);
            const uint32_t y1 = std::min(2 * y + 1, source.height - 1);
            for (uint32_t x = 0; x < result.size.width; ++x)
            {
                const uint32_t x0 = std::min(2 * x, source.width - 1);
                const uint32_t x1 = std::min(2 * x + 1, source.width - 1);
                for (uint32_t c = 0; c < 4; ++c)
                {
                    const auto at = [&](uint32_t px, uint32_t py) { return image.pixels[(size_t{py} * source.width + px) * 4 + c]; };
                    result.pixels[(size_t{y} * result.size.width + x) * 4 + c] =
                        0.25f * (at(x0, y0) + at(x1, y0) + at(x0, y1) + at(x1, y1));
                }
            }
        }
        return result;
    }

    std::vector<HdrImage> generateMipChain(HdrImage base)
    {
        const uint32_t levels = rhi::fullMipCount(base.size);
        std::vector<HdrImage> chain;
        chain.reserve(levels);
        chain.push_back(std::move(base));
        for (uint32_t level = 1; level < levels; ++level)
            chain.push_back(downsample(chain.back()));
        return chain;
    }

    uint16_t floatToHalf(float value)
    {
        if (std::isnan(value))
            return 0;
        value = std::clamp(value, -65504.0f, 65504.0f);
        const auto bits = std::bit_cast<uint32_t>(value);
        const uint32_t sign = (bits >> 16) & 0x8000u;
        const uint32_t magnitude = bits & 0x7FFFFFFFu;

        // Below the smallest normal half (2^-14): a subnormal, mantissa * 2^-24. With the implicit
        // leading bit, the float is mantissa * 2^(exponent - 150), so the half's mantissa is the
        // float's shifted right by 126 - exponent, rounded half to even.
        if (magnitude < 0x38800000u)
        {
            if (magnitude < 0x33000000u)   // Below half the smallest subnormal: rounds to zero.
                return static_cast<uint16_t>(sign);
            const uint32_t exponent = magnitude >> 23;
            const uint32_t mantissa = (magnitude & 0x7FFFFFu) | 0x800000u;
            const uint32_t shift = 126u - exponent;
            const uint32_t halfMantissa = mantissa >> shift;
            const uint32_t remainder = mantissa & ((1u << shift) - 1u);
            const uint32_t halfway = 1u << (shift - 1u);
            const uint32_t rounded = halfMantissa + ((remainder > halfway || (remainder == halfway && (halfMantissa & 1u))) ? 1u : 0u);
            return static_cast<uint16_t>(sign | rounded);
        }

        // Normal: rebias the exponent from 127 to 15 and keep the top 10 mantissa bits, rounded half
        // to even. A carry out of the mantissa correctly bumps the exponent.
        const uint32_t rebiased = magnitude - 0x38000000u;
        const uint32_t remainder = rebiased & 0x1FFFu;
        uint32_t half = rebiased >> 13;
        if (remainder > 0x1000u || (remainder == 0x1000u && (half & 1u)))
            ++half;
        return static_cast<uint16_t>(sign | half);
    }

    float halfToFloat(uint16_t half)
    {
        const uint32_t sign = (uint32_t{half} & 0x8000u) << 16;
        const uint32_t exponent = (half >> 10) & 0x1Fu;
        const uint32_t mantissa = half & 0x3FFu;
        if (exponent == 0)
        {
            // Subnormal (or zero): mantissa * 2^-24.
            const float value = static_cast<float>(mantissa) * 5.9604644775390625e-8f;
            return sign ? -value : value;
        }
        if (exponent == 31)
            return std::bit_cast<float>(sign | 0x7F800000u | (mantissa << 13));
        return std::bit_cast<float>(sign | ((exponent + 112u) << 23) | (mantissa << 13));
    }

    std::vector<std::byte> toHalfFloatPixels(const HdrImage& image)
    {
        std::vector<std::byte> bytes(image.pixels.size() * sizeof(uint16_t));
        for (size_t i = 0; i < image.pixels.size(); ++i)
        {
            const uint16_t half = floatToHalf(std::max(image.pixels[i], 0.0f));
            std::memcpy(bytes.data() + i * sizeof(uint16_t), &half, sizeof(half));
        }
        return bytes;
    }
}

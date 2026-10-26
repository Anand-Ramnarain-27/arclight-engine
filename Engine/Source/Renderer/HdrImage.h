#pragma once

#include "Platform/Window.h"

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace arc
{
    /// Linear RGBA pixels as 32-bit floats, rows from top to bottom, tightly packed: an image whose
    /// values may go far above 1, such as a photographed sky with the sun in it.
    struct HdrImage
    {
        Extent2D size;              // Pixels.
        std::vector<float> pixels;  // size.width * size.height * 4 floats.

        [[nodiscard]] bool empty() const { return pixels.empty(); }
    };

    /// Decodes a Radiance .hdr (RGBE) file. Returns an empty image and logs on failure.
    [[nodiscard]] HdrImage decodeHdrImage(std::span<const std::byte> encoded, std::string_view debugName);
    [[nodiscard]] HdrImage loadHdrImage(const std::string& path);

    /// Halves each dimension (rounding down, never below 1) with a 2x2 box filter.
    [[nodiscard]] HdrImage downsample(const HdrImage& image);

    /// Every mip level down to 1x1, level 0 first.
    [[nodiscard]] std::vector<HdrImage> generateMipChain(HdrImage base);

    /// IEEE 754 half precision, rounded to nearest. Values beyond the largest half (65504) become
    /// 65504 rather than infinity, and NaN becomes 0, so a sun brighter than half floats can hold
    /// still filters and sums to finite values.
    [[nodiscard]] uint16_t floatToHalf(float value);
    [[nodiscard]] float halfToFloat(uint16_t half);

    /// The pixels as RGBA16_FLOAT texture data. Negative values become 0.
    [[nodiscard]] std::vector<std::byte> toHalfFloatPixels(const HdrImage& image);
}

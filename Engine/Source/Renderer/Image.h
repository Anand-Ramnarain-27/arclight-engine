#pragma once

#include "Platform/Window.h"

#include <cstddef>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace arc
{
    /// 8-bit RGBA pixels in CPU memory, rows from top to bottom, tightly packed.
    struct Image
    {
        Extent2D size;                  // Pixels.
        std::vector<std::byte> pixels;  // size.width * size.height * 4 bytes.

        [[nodiscard]] bool empty() const { return pixels.empty(); }
    };

    /// How an image's stored values relate to light. Colour images (photos, albedo, UI) are
    /// almost always sRGB-encoded; data such as normal maps, roughness or masks is linear.
    enum class ColorSpace
    {
        Srgb,
        Linear,
    };

    /// Decodes a PNG, JPEG, TGA or BMP file into RGBA8. Returns an empty image and logs on failure.
    [[nodiscard]] Image loadImage(const std::string& path);

    /// The same, from encoded bytes already in memory. `debugName` is only used in log messages.
    [[nodiscard]] Image decodeImage(std::span<const std::byte> encoded, std::string_view debugName);

    /// Halves each dimension (rounding down, never below 1) with a 2x2 box filter. sRGB images are
    /// averaged in linear space, so a black and white checkerboard becomes mid grey (188), not 128.
    /// Alpha is always linear.
    [[nodiscard]] Image downsample(const Image& image, ColorSpace colorSpace);

    /// Every mip level down to 1x1, level 0 first. Each level is downsample() of the previous one.
    [[nodiscard]] std::vector<Image> generateMipChain(Image base, ColorSpace colorSpace);
}

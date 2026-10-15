#include "Renderer/Image.h"

#include <doctest/doctest.h>

#include <array>
#include <cstddef>
#include <cstdint>

using namespace arc;

namespace
{
    Image solid(Extent2D size, std::array<uint8_t, 4> rgba)
    {
        Image image;
        image.size = size;
        image.pixels.resize(size_t{size.width} * size.height * 4);
        for (size_t i = 0; i < image.pixels.size(); ++i)
            image.pixels[i] = std::byte{rgba[i % 4]};
        return image;
    }

    // Two black and two white pixels in a 2x2 image, with alpha 0 on the black ones.
    Image blackAndWhite()
    {
        Image image = solid({2, 2}, {0, 0, 0, 0});
        for (size_t pixel : {size_t{1}, size_t{2}})
            for (size_t c = 0; c < 4; ++c)
                image.pixels[pixel * 4 + c] = std::byte{255};
        return image;
    }

    uint8_t channel(const Image& image, uint32_t x, uint32_t y, uint32_t c)
    {
        return std::to_integer<uint8_t>(image.pixels[(size_t{y} * image.size.width + x) * 4 + c]);
    }

    // A 2x2 RGBA PNG: red, green / blue, white at half alpha.
    constexpr std::array<unsigned char, 82> kTinyPng = {
        0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A, 0x00, 0x00, 0x00, 0x0D, 0x49, 0x48, 0x44, 0x52, 0x00,
        0x00, 0x00, 0x02, 0x00, 0x00, 0x00, 0x02, 0x08, 0x06, 0x00, 0x00, 0x00, 0x72, 0xB6, 0x0D, 0x24, 0x00,
        0x00, 0x00, 0x19, 0x49, 0x44, 0x41, 0x54, 0x78, 0xDA, 0x05, 0xC1, 0x01, 0x0D, 0x00, 0x00, 0x08, 0xC0,
        0x20, 0xDC, 0x2C, 0x6E, 0xF2, 0x0B, 0x22, 0x69, 0x47, 0xE2, 0x1E, 0x3F, 0x80, 0x06, 0x81, 0xA3, 0x55,
        0xFE, 0xF0, 0x00, 0x00, 0x00, 0x00, 0x49, 0x45, 0x4E, 0x44, 0xAE, 0x42, 0x60, 0x82,
    };
}

TEST_CASE("Downsampling halves the size, rounding down")
{
    const Image half = downsample(solid({5, 3}, {10, 20, 30, 40}), ColorSpace::Linear);
    CHECK(half.size.width == 2);
    CHECK(half.size.height == 1);
    CHECK(half.pixels.size() == 2 * 1 * 4);
    CHECK(channel(half, 1, 0, 2) == 30);

    const Image column = downsample(solid({1, 4}, {1, 2, 3, 4}), ColorSpace::Linear);
    CHECK(column.size.width == 1);
    CHECK(column.size.height == 2);
}

TEST_CASE("Linear images are averaged as stored")
{
    const Image average = downsample(blackAndWhite(), ColorSpace::Linear);
    REQUIRE(average.size.width == 1);
    CHECK(channel(average, 0, 0, 0) == 128);
    CHECK(channel(average, 0, 0, 3) == 128);
}

TEST_CASE("sRGB images are averaged in linear space, alpha is not")
{
    // Half the light of white is linear 0.5, which sRGB encodes as 188, not 128.
    const Image average = downsample(blackAndWhite(), ColorSpace::Srgb);
    CHECK(channel(average, 0, 0, 0) == 188);
    CHECK(channel(average, 0, 0, 1) == 188);
    CHECK(channel(average, 0, 0, 2) == 188);
    CHECK(channel(average, 0, 0, 3) == 128);
}

TEST_CASE("A mip chain has every level down to 1x1")
{
    const std::vector<Image> chain = generateMipChain(solid({37, 19}, {50, 100, 150, 255}), ColorSpace::Srgb);
    REQUIRE(chain.size() == 6);
    CHECK(chain[1].size.width == 18);
    CHECK(chain[5].size.width == 1);
    CHECK(chain[5].size.height == 1);
    // A uniform colour stays the same at every level, in either colour space.
    CHECK(channel(chain[5], 0, 0, 0) == 50);
    CHECK(channel(chain[5], 0, 0, 2) == 150);
}

TEST_CASE("PNG files decode to RGBA8, top row first")
{
    const Image image = decodeImage(std::as_bytes(std::span(kTinyPng)), "tiny.png");
    REQUIRE(image.size.width == 2);
    REQUIRE(image.size.height == 2);
    CHECK(channel(image, 0, 0, 0) == 255);
    CHECK(channel(image, 1, 0, 1) == 255);
    CHECK(channel(image, 0, 1, 2) == 255);
    CHECK(channel(image, 1, 1, 3) == 128);
}

TEST_CASE("Bytes that are not an image decode to an empty image")
{
    const std::array<std::byte, 4> garbage{};
    CHECK(decodeImage(garbage, "garbage").empty());
}

#include "Assets/AssetImporters.h"
#include "RHI/Backends.h"
#include "RHI/Device.h"
#include "RHI/Texture.h"
#include "RHI/TextureUtils.h"
#include "Renderer/TextureEncoding.h"
#include "Renderer/TextureLoader.h"

#include <doctest/doctest.h>

#include <cmath>
#include <cstdint>
#include <vector>

using namespace arc;

namespace
{
    // Smooth gradients in every channel plus a ripple, like a photograph: no format reproduces it
    // exactly, so each one's error shows.
    Image testImage(Extent2D size)
    {
        Image image;
        image.size = size;
        image.pixels.resize(size_t{size.width} * size.height * 4);
        for (uint32_t y = 0; y < size.height; ++y)
            for (uint32_t x = 0; x < size.width; ++x)
            {
                const float u = static_cast<float>(x) / static_cast<float>(size.width);
                const float v = static_cast<float>(y) / static_cast<float>(size.height);
                const float ripple = 0.5f + 0.5f * std::sin((u + v) * 12.0f);
                const float channels[4] = {u, v, ripple, 1.0f - 0.5f * u * v};
                for (int c = 0; c < 4; ++c)
                    image.pixels[(size_t{y} * size.width + x) * 4 + static_cast<size_t>(c)] =
                        static_cast<std::byte>(std::lround(channels[c] * 255.0f));
            }
        return image;
    }

    // Peak signal-to-noise ratio over the given channels, in decibels: 20 log10(255 / RMS error).
    double psnr(const Image& a, const Image& b, std::initializer_list<int> channels)
    {
        double sum = 0.0;
        size_t count = 0;
        for (size_t i = 0; i < a.pixels.size(); i += 4)
            for (const int c : channels)
            {
                const double d = static_cast<double>(a.pixels[i + static_cast<size_t>(c)]) -
                                 static_cast<double>(b.pixels[i + static_cast<size_t>(c)]);
                sum += d * d;
                ++count;
            }
        const double mse = sum / static_cast<double>(count);
        return mse == 0.0 ? 99.0 : 20.0 * std::log10(255.0 / std::sqrt(mse));
    }

    double roundTripPsnr(rhi::Format format, std::initializer_list<int> channels)
    {
        const Image image = testImage({64, 64});
        const EncodedTexture encoded = encodeTexture(std::span(&image, 1), format);
        REQUIRE(encoded.levels.size() == 1);
        CHECK(encoded.levels[0].size() == rhi::imageBytes(format, image.size));
        const std::vector<Image> decoded = decodeTexture(encoded);
        REQUIRE(decoded.size() == 1);
        return psnr(image, decoded[0], channels);
    }
}

TEST_CASE("Block-compressed formats store 4x4 blocks")
{
    using rhi::Format;
    CHECK(rhi::isBlockCompressed(Format::BC7_UNORM_SRGB));
    CHECK_FALSE(rhi::isBlockCompressed(Format::RGBA8_UNORM));
    CHECK(rhi::bytesPerBlock(Format::BC1_UNORM) == 8);
    CHECK(rhi::bytesPerBlock(Format::BC4_UNORM) == 8);
    CHECK(rhi::bytesPerBlock(Format::BC5_UNORM) == 16);
    CHECK(rhi::bytesPerBlock(Format::RGBA8_UNORM) == 4);
    // Partial blocks round up.
    CHECK(rhi::rowBytes(Format::BC1_UNORM, 5) == 16);
    CHECK(rhi::rowCount(Format::BC7_UNORM, 1) == 1);
    CHECK(rhi::imageBytes(Format::BC7_UNORM, {2048, 2048}) == 2048 * 2048);
    CHECK(rhi::imageBytes(Format::BC1_UNORM, {2048, 2048}) == 2048 * 2048 / 2);
    CHECK(rhi::imageBytes(Format::RGBA8_UNORM, {3, 3}) == 36);
    CHECK(rhi::isSrgbFormat(Format::BC3_UNORM_SRGB));
    CHECK_FALSE(rhi::isSrgbFormat(Format::BC5_UNORM));

    const rhi::TextureCopyLayout layout = rhi::computeTextureCopyLayout(
        {.size = {8, 8}, .format = Format::BC7_UNORM, .mipLevels = 4, .debugName = "Blocks"});
    REQUIRE(layout.levels.size() == 4);
    CHECK(layout.levels[0].rowBytes == 32);
    CHECK(layout.levels[0].rowCount == 2);
    CHECK(layout.levels[2].size.width == 2);
    CHECK(layout.levels[2].rowBytes == 16);
    CHECK(layout.levels[2].rowCount == 1);
}

TEST_CASE("Each image gets the format made for what it holds")
{
    using rhi::Format;
    const Extent2D size{256, 128};
    const auto choose = [&](TextureContent content, ColorSpace space, bool alpha, TextureCompression compression) {
        return chooseTextureFormat(content, space, alpha, compression, size);
    };
    CHECK(choose(TextureContent::Color, ColorSpace::Srgb, false, TextureCompression::Quality) == Format::BC7_UNORM_SRGB);
    CHECK(choose(TextureContent::Color, ColorSpace::Srgb, false, TextureCompression::Small) == Format::BC1_UNORM_SRGB);
    CHECK(choose(TextureContent::Color, ColorSpace::Srgb, true, TextureCompression::Small) == Format::BC3_UNORM_SRGB);
    CHECK(choose(TextureContent::Data, ColorSpace::Linear, false, TextureCompression::Quality) == Format::BC7_UNORM);
    CHECK(choose(TextureContent::Data, ColorSpace::Linear, true, TextureCompression::Small) == Format::BC1_UNORM);
    CHECK(choose(TextureContent::Normal, ColorSpace::Linear, false, TextureCompression::Small) == Format::BC5_UNORM);
    CHECK(choose(TextureContent::Mask, ColorSpace::Linear, false, TextureCompression::Quality) == Format::BC4_UNORM);
    CHECK(choose(TextureContent::Normal, ColorSpace::Linear, false, TextureCompression::None) == Format::RGBA8_UNORM);
    CHECK(chooseTextureFormat(TextureContent::Color, ColorSpace::Srgb, false, TextureCompression::Quality, {30, 30}) ==
          Format::RGBA8_UNORM_SRGB);
}

TEST_CASE("Compressed textures decode close to their source")
{
    using rhi::Format;
    // For this image: BC7 about 39 dB (37 with sRGB's perceptual weighting, which spends bits where
    // the eye looks rather than where PSNR does), BC1 and BC3's colour about 34, and the one- and
    // two-channel formats near exact on its linear ramps.
    const double bc7 = roundTripPsnr(Format::BC7_UNORM, {0, 1, 2});
    const double bc1 = roundTripPsnr(Format::BC1_UNORM, {0, 1, 2});
    CHECK(bc7 > 37.0);
    CHECK(bc1 > 31.0);
    CHECK(bc7 > bc1 + 4.0);
    CHECK(roundTripPsnr(Format::BC7_UNORM_SRGB, {0, 1, 2, 3}) > 35.0);
    CHECK(roundTripPsnr(Format::BC3_UNORM, {0, 1, 2}) > 31.0);
    CHECK(roundTripPsnr(Format::BC3_UNORM, {3}) > 45.0);
    CHECK(roundTripPsnr(Format::BC4_UNORM, {0}) > 45.0);
    CHECK(roundTripPsnr(Format::BC5_UNORM, {0, 1}) > 45.0);
}

TEST_CASE("Single-channel formats sample with zeros in the missing channels")
{
    const Image image = testImage({8, 8});
    const std::vector<Image> bc4 = decodeTexture(encodeTexture(std::span(&image, 1), rhi::Format::BC4_UNORM));
    const std::vector<Image> bc5 = decodeTexture(encodeTexture(std::span(&image, 1), rhi::Format::BC5_UNORM));
    for (size_t i = 0; i < bc4[0].pixels.size(); i += 4)
    {
        CHECK(bc4[0].pixels[i + 1] == std::byte{0});
        CHECK(bc4[0].pixels[i + 2] == std::byte{0});
        CHECK(bc4[0].pixels[i + 3] == std::byte{255});
        CHECK(bc5[0].pixels[i + 2] == std::byte{0});
        CHECK(bc5[0].pixels[i + 3] == std::byte{255});
    }
}

TEST_CASE("A mip chain encodes down to 1x1, each level in whole blocks")
{
    const std::vector<Image> levels = generateMipChain(testImage({16, 8}), ColorSpace::Srgb);
    const EncodedTexture encoded = encodeTexture(levels, rhi::Format::BC1_UNORM_SRGB);
    REQUIRE(encoded.levels.size() == 5);
    CHECK(encoded.levels[0].size() == 4 * 2 * 8);
    CHECK(encoded.levels[4].size() == 8);   // 1x1, one block.
    const std::vector<Image> decoded = decodeTexture(encoded);
    REQUIRE(decoded.size() == 5);
    CHECK(decoded[4].size.width == 1);
    CHECK(decoded[4].pixels.size() == 4);
    CHECK(encoded.byteSize() == 64 + 16 + 8 + 8 + 8);

    const EncodedTexture raw = encodeTexture(levels, rhi::Format::RGBA8_UNORM_SRGB);
    CHECK(raw.levels[0] == levels[0].pixels);
}

TEST_CASE("Block-compressed textures upload and read back through the copy layout")
{
    auto device = rhi::createNullDevice({});
    REQUIRE(device);
    const std::vector<Image> levels = generateMipChain(testImage({12, 8}), ColorSpace::Linear);
    const EncodedTexture encoded = encodeTexture(levels, rhi::Format::BC7_UNORM);
    auto texture = createTexture(*device, encoded, "Blocks");
    REQUIRE(texture);
    CHECK(texture->desc().format == rhi::Format::BC7_UNORM);
    CHECK(texture->desc().mipLevels == 4);
    for (uint32_t mip = 0; mip < 4; ++mip)
        CHECK(rhi::readbackTexture(*device, *texture, mip) == encoded.levels[mip]);
}

TEST_CASE("Images used by several slots keep the channels every slot needs")
{
    ModelData data;
    data.images.resize(5);
    for (int32_t i = 0; i < 5; ++i)
        data.textures.push_back({.image = i, .sampler = {}});
    MaterialData first;
    first.baseColorTexture = 0;
    first.normalTexture = 1;
    first.metallicRoughnessTexture = 2;
    first.occlusionTexture = 2;   // Packed into the red channel of the metallic-roughness image.
    MaterialData second;
    second.occlusionTexture = 3;
    second.emissiveTexture = 1;   // The normal map reused as colour.
    data.materials = {first, second};

    const std::vector<TextureContent> contents = imageContents(data);
    REQUIRE(contents.size() == 5);
    CHECK(contents[0] == TextureContent::Color);
    CHECK(contents[1] == TextureContent::Color);
    CHECK(contents[2] == TextureContent::Data);
    CHECK(contents[3] == TextureContent::Mask);
    CHECK(contents[4] == TextureContent::Color);   // Unused.
}

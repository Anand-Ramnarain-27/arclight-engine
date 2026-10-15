#include "RHI/Backends.h"
#include "RHI/CommandList.h"
#include "RHI/Device.h"
#include "RHI/Sampler.h"
#include "RHI/Texture.h"
#include "RHI/TextureUtils.h"

#include <doctest/doctest.h>

#include <cstddef>
#include <vector>

using namespace arc;
using namespace arc::rhi;

TEST_CASE("A full mip chain goes down to 1x1")
{
    CHECK(fullMipCount({1, 1}) == 1);
    CHECK(fullMipCount({256, 256}) == 9);
    CHECK(fullMipCount({37, 19}) == 6);
    CHECK(fullMipCount({1024, 1}) == 11);
}

TEST_CASE("Mip sizes halve, round down and stop at 1")
{
    CHECK(mipSize({37, 19}, 1).width == 18);
    CHECK(mipSize({37, 19}, 1).height == 9);
    CHECK(mipSize({37, 19}, 5).width == 1);
    CHECK(mipSize({37, 19}, 5).height == 1);
    CHECK(mipSize({1, 8}, 2).width == 1);
    CHECK(mipSize({1, 8}, 2).height == 2);
}

TEST_CASE("The texture copy layout pads rows and aligns levels")
{
    const TextureCopyLayout layout = computeTextureCopyLayout({.size = {37, 19}, .format = Format::RGBA8_UNORM, .mipLevels = 3});
    REQUIRE(layout.levels.size() == 3);

    for (const TextureCopyLayout::Level& level : layout.levels)
    {
        CHECK(level.offset % kTextureCopyOffsetAlignment == 0);
        CHECK(level.rowPitch % kTextureCopyRowAlignment == 0);
        CHECK(level.rowPitch >= level.rowBytes);
    }

    CHECK(layout.levels[0].rowBytes == 37 * 4);
    CHECK(layout.levels[0].rowPitch == 256);
    CHECK(layout.levels[1].offset == 5120);   // 19 rows of 256 bytes is 4864, rounded up to 512.
    CHECK(layout.levels[2].size.width == 9);
    CHECK(layout.levels[2].size.height == 4);
    CHECK(layout.totalSize == layout.levels[2].offset + 256 * 4);
}

TEST_CASE("Texture upload then readback returns the same bytes for every mip level")
{
    auto device = createNullDevice({});
    REQUIRE(device);
    CHECK(verifyTextureRoundTrip(*device));
}

TEST_CASE("Uploaded textures are ready to sample and keep their state across submissions")
{
    auto device = createNullDevice({});
    REQUIRE(device);

    auto texture = device->createTexture({.size = {4, 4}, .format = Format::RGBA8_UNORM_SRGB, .debugName = "State"});
    REQUIRE(texture);
    CHECK(texture->state() == ResourceState::Common);

    const std::vector<std::byte> pixels(4 * 4 * 4, std::byte{0x7F});
    const std::vector<std::span<const std::byte>> levels = {pixels};
    REQUIRE(uploadTexture(*device, *texture, levels));
    CHECK(texture->state() == ResourceState::ShaderRead);

    device->submitImmediate([](CommandList&) {});
    CHECK(texture->state() == ResourceState::ShaderRead);

    CHECK(readbackTexture(*device, *texture, 0) == pixels);
    CHECK(texture->state() == ResourceState::ShaderRead);
}

TEST_CASE("A sampler keeps the description it was created from")
{
    auto device = createNullDevice({});
    REQUIRE(device);

    auto sampler = device->createSampler({.minFilter = Filter::Nearest,
                                          .addressU = AddressMode::ClampToBorder,
                                          .borderColor = BorderColor::OpaqueWhite,
                                          .maxAnisotropy = 8,
                                          .maxLod = 2.0f});
    REQUIRE(sampler);
    CHECK(sampler->desc().minFilter == Filter::Nearest);
    CHECK(sampler->desc().magFilter == Filter::Linear);
    CHECK(sampler->desc().addressU == AddressMode::ClampToBorder);
    CHECK(sampler->desc().addressV == AddressMode::Repeat);
    CHECK(sampler->desc().maxAnisotropy == 8);
    CHECK(sampler->desc().maxLod == 2.0f);
}

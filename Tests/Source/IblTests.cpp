#include "RHI/Backends.h"
#include "RHI/CommandList.h"
#include "RHI/Device.h"
#include "RHI/Sampler.h"
#include "RHI/Texture.h"
#include "Renderer/Brdf.h"
#include "Renderer/EnvironmentMaps.h"
#include "Renderer/HdrImage.h"
#include "Renderer/Lighting.h"

#include <doctest/doctest.h>

#include <cmath>
#include <cstring>
#include <limits>
#include <string>
#include <vector>

using namespace arc;
using namespace arc::rhi;

namespace
{
    std::vector<std::byte> bytes(const std::string& text)
    {
        std::vector<std::byte> result(text.size());
        std::memcpy(result.data(), text.data(), text.size());
        return result;
    }

    HdrImage solidHdr(Extent2D size, float value)
    {
        HdrImage image;
        image.size = size;
        image.pixels.assign(size_t{size.width} * size.height * 4, value);
        return image;
    }
}

TEST_CASE("Half floats round-trip what they can hold and clamp what they cannot")
{
    for (const float value : {0.0f, 1.0f, -2.0f, 0.5f, 1024.0f, 65504.0f})
    {
        CAPTURE(value);
        CHECK(halfToFloat(floatToHalf(value)) == value);
    }
    CHECK(floatToHalf(1.0f) == 0x3C00);
    CHECK(floatToHalf(-2.0f) == 0xC000);
    // 11 significant bits: a relative error of at most 2^-11.
    CHECK(halfToFloat(floatToHalf(0.1f)) == doctest::Approx(0.1f).epsilon(1.0 / 2048));
    CHECK(halfToFloat(floatToHalf(3.14159f)) == doctest::Approx(3.14159f).epsilon(1.0 / 2048));

    // Too bright for a half: the largest one, never infinity, which would poison every filter.
    CHECK(halfToFloat(floatToHalf(1e6f)) == 65504.0f);
    CHECK(halfToFloat(floatToHalf(std::numeric_limits<float>::infinity())) == 65504.0f);
    CHECK(floatToHalf(std::numeric_limits<float>::quiet_NaN()) == 0);

    // Subnormals reach down to 2^-24.
    const float smallest = std::ldexp(1.0f, -24);
    CHECK(floatToHalf(smallest) == 0x0001);
    CHECK(halfToFloat(0x0001) == smallest);
    CHECK(halfToFloat(floatToHalf(1e-5f)) == doctest::Approx(1e-5f).epsilon(0.01));
    CHECK(floatToHalf(1e-9f) == 0);
}

TEST_CASE("HDR pixels become half floats, without negative light")
{
    HdrImage image = solidHdr({2, 1}, 2.0f);
    image.pixels[4] = -1.0f;
    const std::vector<std::byte> halves = toHalfFloatPixels(image);
    REQUIRE(halves.size() == 2 * 4 * sizeof(uint16_t));
    uint16_t values[8];
    std::memcpy(values, halves.data(), sizeof(values));
    CHECK(halfToFloat(values[0]) == 2.0f);
    CHECK(halfToFloat(values[4]) == 0.0f);
}

TEST_CASE("A Radiance .hdr file decodes to linear values above 1")
{
    // Two RGBE pixels, uncompressed: (128, 64, 32) * 2^(129 - 136) and (128, 128, 128) * 2^(131 - 136).
    std::string file = "#?RADIANCE\nFORMAT=32-bit_rle_rgbe\n\n-Y 1 +X 2\n";
    file += std::string{'\x80', '\x40', '\x20', '\x81', '\x80', '\x80', '\x80', '\x83'};
    const HdrImage image = decodeHdrImage(bytes(file), "test.hdr");
    REQUIRE(image.size.width == 2);
    REQUIRE(image.size.height == 1);
    REQUIRE(image.pixels.size() == 8);
    CHECK(image.pixels[0] == doctest::Approx(1.0f));
    CHECK(image.pixels[1] == doctest::Approx(0.5f));
    CHECK(image.pixels[2] == doctest::Approx(0.25f));
    CHECK(image.pixels[3] == 1.0f);
    CHECK(image.pixels[4] == doctest::Approx(4.0f));

    CHECK(decodeHdrImage(bytes("not an image"), "bad.hdr").empty());
}

TEST_CASE("HDR mips average the light, however bright")
{
    HdrImage image = solidHdr({4, 2}, 0.0f);
    image.pixels[0] = 4000.0f;   // One bright texel in the top-left 2x2 block.
    const std::vector<HdrImage> mips = generateMipChain(image);
    REQUIRE(mips.size() == 3);
    CHECK(mips[1].size.width == 2);
    CHECK(mips[1].size.height == 1);
    CHECK(mips[1].pixels[0] == doctest::Approx(1000.0f));
    CHECK(mips[2].size.width == 1);
    CHECK(mips[2].pixels[0] == doctest::Approx(500.0f));
}

TEST_CASE("Hammersley points spread evenly over the unit square")
{
    CHECK(brdf::hammersley(0, 4).x == 0.0f);
    CHECK(brdf::hammersley(0, 4).y == 0.0f);
    CHECK(brdf::hammersley(1, 4).y == 0.5f);
    CHECK(brdf::hammersley(2, 4).y == 0.25f);
    CHECK(brdf::hammersley(3, 4).y == 0.75f);
    CHECK(brdf::hammersley(3, 4).x == 0.75f);
}

TEST_CASE("GGX importance samples are unit half vectors around the normal, tighter when smooth")
{
    const Vec3 normal = normalize(Vec3{0.3f, 0.8f, -0.5f});
    float smoothSpread = 0.0f;
    float roughSpread = 0.0f;
    constexpr uint32_t kCount = 256;
    for (uint32_t i = 0; i < kCount; ++i)
    {
        const Vec2 xi = brdf::hammersley(i, kCount);
        const Vec3 smooth = brdf::importanceSampleGgx(xi, normal, 0.05f);
        const Vec3 rough = brdf::importanceSampleGgx(xi, normal, 1.0f);
        CHECK(length(smooth) == doctest::Approx(1.0f));
        CHECK(dot(smooth, normal) >= 0.0f);
        smoothSpread += 1.0f - dot(smooth, normal);
        roughSpread += 1.0f - dot(rough, normal);
    }
    CHECK(smoothSpread < roughSpread * 0.1f);
}

TEST_CASE("The importance-sampled BRDF table matches the BRDF integrated over the hemisphere")
{
    // f0 * A + B at f0 = 1 is the white directional albedo; at f0 = 0 it is B. The midpoint rule
    // here is independent of the sampling, so agreement checks the pdf and weights in IblBrdfLut.hlsl.
    for (const float roughness : {0.3f, 0.6f, 1.0f})
        for (const float viewAngle : {0.0f, toRadians(60.0f)})
        {
            CAPTURE(roughness);
            CAPTURE(viewAngle);
            const float nDotV = std::cos(viewAngle);
            const Vec3 normal{0.0f, 0.0f, 1.0f};
            const Vec3 view{std::sin(viewAngle), 0.0f, nDotV};
            const float alpha = roughness * roughness;
            double white = 0.0;
            double black = 0.0;
            constexpr int kTheta = 400;
            constexpr int kPhi = 200;
            const double dTheta = kHalfPi / kTheta;
            const double dPhi = kTwoPi / kPhi;
            for (int i = 0; i < kTheta; ++i)
                for (int j = 0; j < kPhi; ++j)
                {
                    const double theta = (i + 0.5) * dTheta;
                    const double phi = (j + 0.5) * dPhi;
                    const Vec3 light{static_cast<float>(std::sin(theta) * std::cos(phi)),
                                     static_cast<float>(std::sin(theta) * std::sin(phi)),
                                     static_cast<float>(std::cos(theta))};
                    const Vec3 h = normalize(light + view);
                    const double specular = brdf::distributionGgx(std::max(dot(normal, h), 0.0f), alpha) *
                                            brdf::visibilitySmithGgxCorrelated(nDotV, light.z, alpha);
                    const double weight = std::cos(theta) * std::sin(theta) * dTheta * dPhi;
                    white += specular * weight;
                    black += specular * brdf::fresnelSchlick(0.0f, dot(view, h)) * weight;
                }
            const Vec2 table = brdf::integrateEnvBrdf(nDotV, roughness, 4096);
            CHECK(table.x + table.y == doctest::Approx(white).epsilon(0.02));
            CHECK(table.y == doctest::Approx(black).epsilon(0.05));
        }
}

TEST_CASE("The environment lights the scene only when asked to and complete")
{
    auto device = createNullDevice({});
    REQUIRE(device);
    auto cube = device->createTexture(
        {.size = {4, 4}, .format = Format::RGBA16_FLOAT, .type = TextureType::Cube, .debugName = "Cube"});
    auto table = device->createTexture({.size = {4, 4}, .format = Format::RGBA16_FLOAT, .debugName = "Table"});
    REQUIRE(cube);
    REQUIRE(table);

    EnvironmentLighting environment;
    environment.irradiance = cube.get();
    environment.prefiltered = cube.get();
    environment.brdfLut = table.get();
    environment.prefilteredMaxLod = 4.0f;
    environment.intensity = 2.0f;
    environment.rotation = kHalfPi;

    LightingSettings settings;
    // No sampler yet: incomplete, so the hemisphere stays.
    CHECK((makeLightingConstants(settings, 0, &environment).flags & kLightingFlagEnvironment) == 0);

    auto sampler = device->createSampler({.debugName = "Environment"});
    environment.sampler = sampler.get();
    const LightingConstants lit = makeLightingConstants(settings, 0, &environment);
    CHECK((lit.flags & kLightingFlagEnvironment) != 0);
    CHECK(lit.environmentIntensity == 2.0f);
    CHECK(lit.prefilteredMaxLod == 4.0f);
    CHECK(lit.environmentRotation[0] == doctest::Approx(0.0f).epsilon(1e-6));
    CHECK(lit.environmentRotation[1] == doctest::Approx(1.0f));

    settings.ambientSource = AmbientSource::Hemisphere;
    CHECK((makeLightingConstants(settings, 0, &environment).flags & kLightingFlagEnvironment) == 0);
    CHECK((makeLightingConstants(LightingSettings{}, 0).flags & kLightingFlagEnvironment) == 0);
}

TEST_CASE("A render pass can draw into one mip level of one cube face")
{
    auto device = createNullDevice({});
    REQUIRE(device);
    auto cube = device->createTexture({.size = {16, 16},
                                       .format = Format::RGBA16_FLOAT,
                                       .mipLevels = 5,
                                       .usage = TextureUsage::Sampled | TextureUsage::RenderTarget,
                                       .type = TextureType::Cube,
                                       .debugName = "Cube Target"});
    REQUIRE(cube);
    device->submitImmediate([&](CommandList& commandList) {
        for (uint32_t mip = 0; mip < 5; ++mip)
            for (uint32_t face = 0; face < 6; ++face)
            {
                commandList.beginRenderPass({.color = {.texture = cube.get(), .mipLevel = mip, .arrayLayer = face}});
                commandList.endRenderPass();
            }
        commandList.barrier(*cube, ResourceState::ShaderRead);
    });
    CHECK(cube->state() == ResourceState::ShaderRead);
}

TEST_CASE("An equirectangular sky's cube faces are a quarter of its width, within limits")
{
    CHECK(EnvironmentMaps::radianceSizeForEquirect(2048) == 512);
    CHECK(EnvironmentMaps::radianceSizeForEquirect(3000) == 512);
    CHECK(EnvironmentMaps::radianceSizeForEquirect(16384) == EnvironmentMaps::kMaxRadianceSize);
    CHECK(EnvironmentMaps::radianceSizeForEquirect(8) == 16);
}

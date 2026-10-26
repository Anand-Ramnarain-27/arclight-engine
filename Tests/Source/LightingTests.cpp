#include "RHI/Backends.h"
#include "RHI/Buffer.h"
#include "RHI/CommandList.h"
#include "RHI/Device.h"
#include "RHI/Texture.h"
#include "Math/Color.h"
#include "Renderer/LightBuffer.h"
#include "Renderer/Lighting.h"
#include "Scene/LightComponent.h"
#include "Scene/Scene.h"
#include "Scene/SceneSerializer.h"

#include <doctest/doctest.h>

#include <cmath>
#include <cstring>
#include <vector>

using namespace arc;
using namespace arc::rhi;

namespace
{
    bool near(const Vec3& a, const Vec3& b) { return nearlyEqual(a, b, 1e-4f); }

    // The cone fade Lighting.hlsli computes, for a direction `angle` radians off the light's axis.
    float coneFade(const GpuLight& light, float angle)
    {
        const float fade = std::clamp(std::cos(angle) * light.spotScale + light.spotOffset, 0.0f, 1.0f);
        return fade * fade;
    }
}

TEST_CASE("Quat::fromForward turns -Z to the direction without roll")
{
    for (const Vec3 direction : {Vec3{0.0f, 0.0f, -1.0f}, Vec3{1.0f, 0.0f, 0.0f}, Vec3{0.3f, -0.8f, 0.5f},
                                 Vec3{-0.4f, 0.8f, 0.45f}, Vec3{0.0f, -1.0f, 0.0f}, Vec3{0.0f, 2.0f, 0.0f}})
    {
        CAPTURE(direction);
        const Quat rotation = Quat::fromForward(direction);
        CHECK(near(rotation.rotate(Vec3::forward()), normalize(direction)));
        // No roll: local X stays in the horizontal plane.
        CHECK(rotation.rotate(Vec3::right()).y == doctest::Approx(0.0f).epsilon(1e-5));
    }
    // Straight down has no yaw to speak of; it reads as zero.
    CHECK(Quat::fromForward({0.0f, -1.0f, 0.0f}).toEuler().y == doctest::Approx(0.0f));
}

TEST_CASE("Lights are converted to the layout the shaders read")
{
    SUBCASE("Directional lights have no range and a unit direction")
    {
        const LightDesc sun{.type = LightType::Directional, .color = {1.0f, 0.5f, 0.25f}, .intensity = 4.0f, .range = 7.0f};
        const GpuLight light = makeGpuLight(sun, {1.0f, 2.0f, 3.0f}, {0.0f, -5.0f, 0.0f});
        CHECK(light.type == LightType::Directional);
        CHECK(near(light.direction, {0.0f, -1.0f, 0.0f}));
        CHECK(light.range == 0.0f);
        CHECK(near(light.radiance, {4.0f, 2.0f, 1.0f}));
        CHECK(coneFade(light, kPi) == 1.0f);
    }

    SUBCASE("Spot cones are full inside the inner angle and zero outside the outer one")
    {
        const LightDesc spot{.type = LightType::Spot,
                             .intensity = 10.0f,
                             .range = 5.0f,
                             .innerConeAngle = toRadians(20.0f),
                             .outerConeAngle = toRadians(30.0f)};
        const GpuLight light = makeGpuLight(spot, Vec3::zero(), Vec3::forward());
        CHECK(coneFade(light, 0.0f) == doctest::Approx(1.0f));
        CHECK(coneFade(light, toRadians(20.0f)) == doctest::Approx(1.0f));
        CHECK(coneFade(light, toRadians(25.0f)) > 0.0f);
        CHECK(coneFade(light, toRadians(25.0f)) < 1.0f);
        CHECK(coneFade(light, toRadians(30.0f)) == doctest::Approx(0.0f).epsilon(1e-4));
        CHECK(coneFade(light, toRadians(60.0f)) == 0.0f);
    }

    SUBCASE("Out-of-range values are clamped")
    {
        const LightDesc bad{.type = LightType::Spot,
                            .color = {-1.0f, 1.0f, 1.0f},
                            .intensity = -3.0f,
                            .range = -1.0f,
                            .innerConeAngle = toRadians(80.0f),
                            .outerConeAngle = toRadians(120.0f)};
        const GpuLight light = makeGpuLight(bad, Vec3::zero(), Vec3::zero());
        CHECK(light.range == 0.0f);
        CHECK(near(light.radiance, Vec3::zero()));
        CHECK(std::isfinite(light.spotScale));
        CHECK(std::isfinite(light.spotOffset));
        CHECK(near(light.direction, {0.0f, -1.0f, 0.0f}));
        // Outer is clamped to 90 degrees: nothing behind the light is lit.
        CHECK(coneFade(light, toRadians(100.0f)) == 0.0f);
    }

    SUBCASE("Point lights ignore the cone")
    {
        const GpuLight light = makeGpuLight({.type = LightType::Point}, Vec3::zero(), Vec3::forward());
        CHECK(coneFade(light, kPi) == 1.0f);
    }
}

TEST_CASE("Light components shine along their object's -Z, unaffected by scale")
{
    Scene scene;
    GameObject& object = scene.create("Spot");
    auto& component = object.addComponent<LightComponent>(defaultLightDesc(LightType::Spot));
    object.transform().setLocal({1.0f, 2.0f, 3.0f}, Quat::fromForward({1.0f, 0.0f, 0.0f}), {4.0f, 4.0f, 4.0f});

    const GpuLight light = component.toGpuLight(object.transform().worldMatrix());
    CHECK(near(light.position, {1.0f, 2.0f, 3.0f}));
    CHECK(near(light.direction, {1.0f, 0.0f, 0.0f}));
    CHECK(light.type == LightType::Spot);
}

TEST_CASE("Only enabled lights of active objects are collected")
{
    Scene scene;
    GameObject& lit = scene.create("Lit");
    lit.addComponent<LightComponent>(defaultLightDesc(LightType::Point));
    lit.addComponent<LightComponent>(defaultLightDesc(LightType::Spot));

    GameObject& disabled = scene.create("Disabled component");
    disabled.addComponent<LightComponent>().enabled = false;

    GameObject& parent = scene.create("Inactive parent");
    parent.setActive(false);
    scene.create("Child", &parent).addComponent<LightComponent>();

    GameObject& destroyed = scene.create("Destroyed");
    destroyed.addComponent<LightComponent>();
    scene.destroy(destroyed);

    std::vector<GpuLight> lights;
    CHECK(collectLights(scene, lights) == 2);
    REQUIRE(lights.size() == 2);
    CHECK(lights[0].type == LightType::Point);
    CHECK(lights[1].type == LightType::Spot);
}

TEST_CASE("New directional lights match the default sun")
{
    Scene scene;
    GameObject& sun = createLight(scene, LightType::Directional);
    CHECK(sun.name() == "Directional Light");
    const LightComponent* component = sun.getComponent<LightComponent>();
    REQUIRE(component);
    const GpuLight light = component->toGpuLight(sun.transform().worldMatrix());
    CHECK(near(light.direction, defaultSunLight().direction));
    CHECK(near(light.radiance, defaultSunLight().radiance));

    GameObject& point = createLight(scene, LightType::Point, {0.0f, 5.0f, 0.0f});
    CHECK(point.name() == "Point Light");
    CHECK(near(point.transform().worldPosition(), {0.0f, 5.0f, 0.0f}));
}

TEST_CASE("Light components round-trip through scene files")
{
    Scene scene;
    GameObject& object = scene.create("Lamp");
    LightDesc desc{.type = LightType::Spot,
                   .color = {1.0f, 0.5f, 0.25f},
                   .intensity = 42.0f,
                   .range = 12.5f,
                   .innerConeAngle = toRadians(15.0f),
                   .outerConeAngle = toRadians(35.0f)};
    object.addComponent<LightComponent>(desc).enabled = false;

    const std::string json = writeScene(scene);
    CHECK(json.find("\"lightType\": \"Spot\"") != std::string::npos);
    CHECK(json.find("\"outerConeDegrees\": 35") != std::string::npos);

    Scene loaded;
    REQUIRE(readScene(loaded, json, {}));
    const LightComponent* light = loaded.roots()[0]->getComponent<LightComponent>();
    REQUIRE(light);
    CHECK_FALSE(light->enabled);
    CHECK(light->light.type == LightType::Spot);
    CHECK(near(light->light.color, desc.color));
    CHECK(light->light.intensity == 42.0f);
    CHECK(light->light.range == 12.5f);
    CHECK(light->light.innerConeAngle == doctest::Approx(desc.innerConeAngle));
    CHECK(light->light.outerConeAngle == doctest::Approx(desc.outerConeAngle));
}

TEST_CASE("Light fields from a file are clamped, and unknown types keep the default")
{
    const std::string json = R"({"format":"arclight-scene","version":1,"objects":[
        {"id":"0000000000000001","name":"Odd","components":[
            {"type":"Light","enabled":true,"lightType":"Laser","intensity":-5,"range":-2,
             "innerConeDegrees":80,"outerConeDegrees":45,"color":[-1,0.5,2]}
        ]}
    ]})";
    Scene scene;
    REQUIRE(readScene(scene, json, {}));
    const LightComponent* light = scene.roots()[0]->getComponent<LightComponent>();
    REQUIRE(light);
    CHECK(light->light.intensity == 0.0f);
    CHECK(light->light.range == 0.0f);
    CHECK(light->light.color.x == 0.0f);
    CHECK(light->light.color.z == 2.0f);
    CHECK(light->light.outerConeAngle == doctest::Approx(toRadians(45.0f)));
    CHECK(light->light.innerConeAngle <= light->light.outerConeAngle);
}

TEST_CASE("Lighting constants carry the settings")
{
    LightingSettings settings;
    settings.model = LightingModel::Phong;
    settings.energyConserving = false;
    settings.fresnel = true;
    settings.ambientIntensity = 2.0f;
    LightingConstants constants = makeLightingConstants(settings, 3);
    CHECK(constants.lightCount == 3);
    CHECK(constants.model == static_cast<uint32_t>(LightingModel::Phong));
    CHECK(constants.flags == (kLightingFlagFresnel | kLightingFlagNormalMaps | kLightingFlagOcclusion | kLightingFlagEmissive));
    CHECK(near(constants.ambientSky, settings.ambientSky * 2.0f));
    CHECK(constants.roughnessOverride < 0.0f);

    CHECK(constants.metallicOverride < 0.0f);

    settings.normalMaps = false;
    settings.occlusion = false;
    settings.emissive = false;
    CHECK(makeLightingConstants(settings, 0).flags == kLightingFlagFresnel);

    settings.overrideRoughness = true;
    settings.roughness = 1.5f;
    settings.overrideMetallic = true;
    settings.metallic = 0.25f;
    constants = makeLightingConstants(settings, 0);
    CHECK(constants.roughnessOverride == 1.0f);
    CHECK(constants.metallicOverride == 0.25f);
}

TEST_CASE("sRGB conversion round-trips and clamps")
{
    for (const float value : {0.0f, 0.002f, 0.04f, 0.2f, 0.5f, 0.9f, 1.0f})
        CHECK(srgbToLinear(linearToSrgb(value)) == doctest::Approx(value).epsilon(1e-4));
    CHECK(linearToSrgb(0.5f) == doctest::Approx(0.7354f).epsilon(1e-3));
    CHECK(linearToSrgb(2.0f) == doctest::Approx(1.0f));
    CHECK(srgbToLinear(-1.0f) == 0.0f);
}

TEST_CASE("HDR render targets and structured buffers can be created and bound")
{
    auto device = createNullDevice({});
    REQUIRE(device);
    CHECK(bytesPerPixel(Format::RGBA16_FLOAT) == 8);

    auto target = device->createTexture({.size = {16, 16},
                                         .format = Format::RGBA16_FLOAT,
                                         .usage = TextureUsage::RenderTarget | TextureUsage::Sampled,
                                         .debugName = "HDR"});
    REQUIRE(target);

    LightBuffer lights;
    REQUIRE(lights.init(*device));
    REQUIRE(lights.buffer(0));
    CHECK(lights.buffer(0)->desc().stride == sizeof(GpuLight));
    CHECK(lights.buffer(0)->desc().size == kMaxLights * sizeof(GpuLight));

    device->submitImmediate([&](CommandList& commandList) {
        RenderPassDesc pass;
        pass.color.texture = target.get();
        commandList.beginRenderPass(pass);
        commandList.setBuffer(kLightBufferSlot, *lights.buffer(0));
        commandList.endRenderPass();
        commandList.barrier(*target, ResourceState::ShaderRead);
    });
    CHECK(target->state() == ResourceState::ShaderRead);
}

TEST_CASE("The light buffer keeps at most kMaxLights")
{
    auto device = createNullDevice({});
    REQUIRE(device);
    LightBuffer lights;
    REQUIRE(lights.init(*device));

    std::vector<GpuLight> many(kMaxLights + 10, defaultSunLight());
    many[1].range = 3.0f;
    CHECK(lights.write(1, many) == kMaxLights);
    GpuLight second;
    std::memcpy(&second, static_cast<const std::byte*>(lights.buffer(1)->mappedData()) + sizeof(GpuLight), sizeof(GpuLight));
    CHECK(second.range == 3.0f);
    CHECK(lights.write(0, {}) == 0);
    lights.shutdown();
}

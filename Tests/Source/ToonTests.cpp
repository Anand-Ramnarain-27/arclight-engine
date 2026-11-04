#include "RHI/Backends.h"
#include "RHI/Device.h"
#include "RHI/Texture.h"
#include "Assets/ResourceManager.h"
#include "Renderer/GeometryPool.h"
#include "Renderer/MaterialStyle.h"
#include "Renderer/Model.h"
#include "Renderer/RenderList.h"
#include "Scene/MeshComponent.h"
#include "Scene/Scene.h"
#include "Scene/SceneSerializer.h"

#include <doctest/doctest.h>

#include <memory>
#include <optional>
#include <set>
#include <string>

using namespace arc;

namespace
{
    // One triangle per material.
    ModelData threeMaterials()
    {
        ModelData data;
        data.name = "Styled";
        for (int32_t p = 0; p < 3; ++p)
        {
            Primitive primitive;
            primitive.firstIndex = static_cast<uint32_t>(data.indices.size());
            primitive.indexCount = 3;
            primitive.baseVertex = static_cast<uint32_t>(data.vertices.size());
            primitive.vertexCount = 3;
            primitive.material = p;
            for (uint32_t v = 0; v < 3; ++v)
            {
                data.vertices.push_back({{static_cast<float>(p), static_cast<float>(v), 0}, {0, 0, 1}, {}});
                primitive.bounds.expand(data.vertices.back().position);
                data.indices.push_back(v);
            }
            data.primitives.push_back(primitive);
        }
        data.materials.resize(3);
        data.meshes = {{.name = "Styled", .firstPrimitive = 0, .primitiveCount = 3}};
        return data;
    }
}

TEST_CASE("Toon bands step from shadow to full light")
{
    CHECK(toonBandLight(-0.5f, 3, 0.02f) == 0.0f);
    CHECK(toonBandLight(1.0f, 3, 0.02f) == doctest::Approx(1.0f));
    // One band is two-tone, split where the light grazes the surface.
    CHECK(toonBandLight(-0.1f, 1, 0.02f) == 0.0f);
    CHECK(toonBandLight(0.1f, 1, 0.02f) == 1.0f);

    std::set<float> levels;
    float previous = 0.0f;
    for (int i = -100; i <= 100; ++i)
    {
        const float light = toonBandLight(static_cast<float>(i) / 100.0f + 0.001f, 3, 0.0f);
        CHECK(light >= previous);
        previous = light;
        levels.insert(light);
    }
    // Shadow plus three steps, and nothing in between with no softness.
    CHECK(levels.size() == 4);
}

TEST_CASE("Render items point at the object's style for their material, when it differs")
{
    auto device = rhi::createNullDevice({});
    REQUIRE(device);
    GeometryPool geometry(*device);
    const auto model = Model::create(*device, geometry, threeMaterials());
    REQUIRE(model);

    MaterialStyle toon;
    toon.shading = ShadingModel::Toon;
    const std::vector<MaterialStyle> styles = {toon, MaterialStyle{}};   // Material 2 has none.

    RenderList list;
    list.addMesh(*model, 0, Mat4::identity(), {}, styles);
    REQUIRE(list.opaque().size() == 3);
    for (const RenderItem& item : list.opaque())
    {
        const int32_t material = model->primitives()[item.primitive].material;
        CHECK(item.style == (material == 0 ? &styles[0] : nullptr));
    }
}

TEST_CASE("Material styles are saved with the mesh component, ramp path included")
{
    auto device = rhi::createNullDevice({});
    REQUIRE(device);
    GeometryPool geometry(*device);
    const std::shared_ptr<const Model> model = Model::create(*device, geometry, threeMaterials(), Uuid{0x77});
    REQUIRE(model);

    MaterialStyle style;
    style.shading = ShadingModel::Toon;
    style.toon.bands = 4;
    style.toon.softness = 0.1f;
    style.toon.rimSize = 0.4f;
    style.outline = {.enabled = true, .width = 3.0f, .color = {0.5f, 0.0f, 0.25f}};

    Scene scene;
    MeshComponent& mesh = scene.create("Styled").addComponent<MeshComponent>(model, 0);
    mesh.setMaterialStyle(1, style);
    // Without a resource manager the texture cannot be loaded, but the path is still kept.
    mesh.setToonRamp(2, "assets://Ramps/warm.png", nullptr);
    CHECK(mesh.materialStyles().size() == 3);
    CHECK(mesh.materialStyle(0).isDefault());
    CHECK(mesh.materialStyle(9).isDefault());

    const std::string json = writeScene(scene);
    CHECK(json.find("\"material1.shading\": \"toon\"") != std::string::npos);
    CHECK(json.find("material0.") == std::string::npos);

    Scene loaded;
    REQUIRE(readScene(loaded, json, {}));
    const MeshComponent* read = loaded.roots()[0]->getComponent<MeshComponent>();
    REQUIRE(read);
    CHECK(read->materialStyle(1) == style);
    CHECK(read->materialStyle(0).isDefault());
    CHECK(read->toonRampPath(2) == "assets://Ramps/warm.png");
    CHECK(writeScene(loaded) == json);
}

TEST_CASE("Changing a style keeps the ramp texture it is using")
{
    auto device = rhi::createNullDevice({});
    REQUIRE(device);
    auto ramp = std::make_shared<TextureAsset>();
    ramp->texture = device->createTexture({.size = {4, 1}, .format = rhi::Format::RGBA8_UNORM_SRGB, .debugName = "Ramp"});
    REQUIRE(ramp->texture);

    MeshComponent mesh;
    mesh.setToonRamp(0, "ramp.png", ramp);
    CHECK(mesh.materialStyle(0).toon.ramp == ramp->texture.get());
    MaterialStyle toon;
    toon.shading = ShadingModel::Toon;
    mesh.setMaterialStyle(0, toon);
    CHECK(mesh.materialStyle(0).toon.ramp == ramp->texture.get());
    CHECK(mesh.materialStyle(0).shading == ShadingModel::Toon);
    mesh.setToonRamp(0, {}, nullptr);
    CHECK(mesh.materialStyle(0).toon.ramp == nullptr);
    CHECK(mesh.toonRampPath(0).empty());
}

TEST_CASE("A whole hierarchy switches shading at once, keeping what each material was tuned to")
{
    auto device = rhi::createNullDevice({});
    REQUIRE(device);
    GeometryPool geometry(*device);
    const std::shared_ptr<const Model> model = Model::create(*device, geometry, threeMaterials());
    REQUIRE(model);
    auto ramp = std::make_shared<TextureAsset>();
    ramp->texture = device->createTexture({.size = {4, 1}, .format = rhi::Format::RGBA8_UNORM_SRGB, .debugName = "Ramp"});

    Scene scene;
    GameObject& room = scene.create("Room");
    MeshComponent& wall = scene.create("Wall", &room).addComponent<MeshComponent>(model, 0);
    GameObject& hidden = scene.create("Hidden", &room);
    hidden.setActive(false);
    MeshComponent& chair = hidden.addComponent<MeshComponent>(model, 0);
    MaterialStyle tuned;
    tuned.toon.bands = 5;
    wall.setMaterialStyle(1, tuned);
    wall.setToonRamp(2, "ramp.png", ramp);

    // Every drawn material of both meshes, the inactive one included.
    CHECK(changeStyles(room, {.shading = ShadingModel::Toon, .outline = std::nullopt}) == 6);
    CHECK(wall.materialStyle(0).shading == ShadingModel::Toon);
    CHECK(chair.materialStyle(2).shading == ShadingModel::Toon);
    CHECK(wall.materialStyle(1).toon.bands == 5);
    CHECK_FALSE(wall.materialStyle(1).outline.enabled);
    CHECK(changeStyles(room, {.shading = ShadingModel::Toon, .outline = std::nullopt}) == 0);

    CHECK(changeStyles(room, {.shading = std::nullopt, .outline = true}) == 6);
    CHECK(chair.materialStyle(0).outline.enabled);
    CHECK(chair.materialStyle(0).shading == ShadingModel::Toon);

    MaterialStyle look;
    look.shading = ShadingModel::Toon;
    look.toon.bands = 3;
    look.outline = {.enabled = true, .width = 4.0f, .color = {1, 0, 0}};
    CHECK(copyStyleToHierarchy(room, look) == 6);
    CHECK(chair.materialStyle(1).toon.bands == 3);
    CHECK(wall.materialStyle(2).outline.width == 4.0f);
    CHECK(wall.materialStyle(2).toon.ramp == ramp->texture.get());
    CHECK(wall.toonRampPath(2) == "ramp.png");
    // Only the object it is called on and what is below it.
    MeshComponent& outside = scene.create("Outside").addComponent<MeshComponent>(model, 0);
    CHECK(changeStyles(room, {.shading = ShadingModel::Physical, .outline = false}) == 6);
    CHECK(outside.materialStyle(0).isDefault());
}

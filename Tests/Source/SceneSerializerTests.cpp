#include "Assets/AssetDatabase.h"
#include "Assets/ResourceManager.h"
#include "Platform/FileSystem.h"
#include "Platform/VirtualFileSystem.h"
#include "RHI/Backends.h"
#include "RHI/Device.h"
#include "Renderer/GeometryPool.h"
#include "Renderer/Model.h"
#include "Scene/ComponentData.h"
#include "Scene/ComponentRegistry.h"
#include "Scene/MeshComponent.h"
#include "Scene/Scene.h"
#include "Scene/SceneSerializer.h"
#include "TestAssets.h"

#include <doctest/doctest.h>

#include <filesystem>
#include <random>
#include <string>
#include <vector>

using namespace arc;

namespace
{
    std::vector<std::string> namesInOrder(const Scene& scene)
    {
        std::vector<std::string> names;
        const auto visit = [&names](const auto& self, const GameObject& object) -> void {
            names.push_back(object.name());
            for (const GameObject* child : object.children())
                self(self, *child);
        };
        for (const GameObject* root : scene.roots())
            visit(visit, *root);
        return names;
    }

    // Root A (inactive child B with grandchild C), then root D.
    void buildSample(Scene& scene)
    {
        GameObject& a = scene.create("A");
        a.transform().setLocal({1, 2, 3}, Quat::fromEuler({0.1f, 0.2f, 0.3f}), {2, 2, 2});
        GameObject& b = scene.create("B", &a);
        b.setActive(false);
        b.transform().setLocalPosition({0.1f, 0, -5});
        scene.create("C", &b);
        scene.create("D");
    }
}

TEST_CASE("Component data stores values and refuses the wrong kind")
{
    ComponentData data;
    CHECK(data.empty());
    data.setBool("visible", true);
    data.setInt("count", -7);
    data.setFloat("speed", 0.1f);
    data.setString("label", "lantern");
    data.setVec3("offset", {1, 2, 3});
    data.setQuat("turn", Quat(0, 0, 0, 1));
    data.setUuid("target", Uuid{0xabc});

    CHECK(data.getBool("visible") == true);
    CHECK(data.getInt("count") == -7);
    CHECK(data.getFloat("speed") == 0.1f);
    CHECK(data.getString("label") == "lantern");
    REQUIRE(data.getVec3("offset"));
    CHECK(data.getVec3("offset")->z == 3.0f);
    REQUIRE(data.getQuat("turn"));
    CHECK(data.getQuat("turn")->w == 1.0f);
    CHECK(data.getUuid("target") == Uuid{0xabc});

    CHECK_FALSE(data.getBool("count"));
    CHECK_FALSE(data.getInt("speed"));
    CHECK_FALSE(data.getString("visible"));
    CHECK_FALSE(data.getVec3("turn"));
    CHECK_FALSE(data.getUuid("label"));
    CHECK_FALSE(data.getFloat("missing"));
    CHECK(data.getFloat("count") == -7.0f);

    const ComponentData copy = data;
    CHECK(copy.getString("label") == "lantern");
    data.setFloat("third", 1.0f / 3.0f);
    CHECK(data.getFloat("third") == 1.0f / 3.0f);
}

TEST_CASE("Scenes round-trip with their hierarchy, ids and transforms")
{
    Scene original;
    buildSample(original);
    const std::string json = writeScene(original);
    CHECK(json.find("\"format\": \"arclight-scene\"") != std::string::npos);
    // Floats are written short, so files diff cleanly.
    CHECK(json.find("0.1,") != std::string::npos);
    CHECK(json.find("0.100000") == std::string::npos);

    Scene loaded;
    loaded.create("Old");
    REQUIRE(readScene(loaded, json, {}));
    CHECK(namesInOrder(loaded) == std::vector<std::string>{"A", "B", "C", "D"});
    REQUIRE(loaded.roots().size() == 2);
    const GameObject& a = *loaded.roots()[0];
    CHECK(a.uuid() == original.roots()[0]->uuid());
    CHECK(a.transform().localPosition().y == 2.0f);
    CHECK(a.transform().localScale().x == 2.0f);
    const Quat rotation = a.transform().localRotation();
    const Quat expected = original.roots()[0]->transform().localRotation();
    CHECK(rotation.x == doctest::Approx(expected.x));
    CHECK(rotation.w == doctest::Approx(expected.w));
    const GameObject& b = *a.children()[0];
    CHECK_FALSE(b.isActiveSelf());
    CHECK(b.children()[0]->name() == "C");
    CHECK(b.children()[0]->uuid() == original.roots()[0]->children()[0]->children()[0]->uuid());

    // Saving what was loaded gives the same file.
    CHECK(writeScene(loaded) == json);
}

TEST_CASE("Bad scene files leave the scene as it was")
{
    Scene scene;
    buildSample(scene);
    const std::string before = writeScene(scene);
    std::string error;

    CHECK_FALSE(readScene(scene, "{ not json", {}, &error));
    CHECK(error == "not a JSON object");
    CHECK_FALSE(readScene(scene, R"({"format":"arclight-prefab","version":1,"objects":[]})", {}, &error));
    CHECK_FALSE(readScene(scene, R"({"format":"arclight-scene","version":99,"objects":[]})", {}, &error));
    CHECK(error.find("newer version") != std::string::npos);
    CHECK_FALSE(readScene(scene, R"({"format":"arclight-scene","version":1,"objects":{}})", {}, &error));
    CHECK_FALSE(readScene(scene, R"({"format":"arclight-scene","version":1,"objects":[3]})", {}, &error));
    CHECK(writeScene(scene) == before);

    REQUIRE(readScene(scene, R"({"format":"arclight-scene","version":1,"objects":[]})", {}));
    CHECK(scene.objectCount() == 0);
}

TEST_CASE("Damaged objects are repaired instead of failing the whole scene")
{
    Scene scene;
    const std::string json = R"({"format":"arclight-scene","version":1,"objects":[
        {"id":"0000000000000001","name":"First","rotation":[0,0,0,0]},
        {"id":"0000000000000001","name":"Same id"},
        {"name":"No id","parent":"0000000000000001","scale":"big"},
        {"id":"0000000000000004","name":"Lost child","parent":"00000000000000ff","rotation":[0,0,2,0]},
        {"id":"0000000000000005"}
    ]})";
    REQUIRE(readScene(scene, json, {}));
    CHECK(scene.objectCount() == 5);
    REQUIRE(scene.roots().size() == 4);
    const GameObject& first = *scene.roots()[0];
    CHECK(first.uuid().value == 1);
    CHECK(first.transform().localRotation().w == 1.0f);
    CHECK(scene.roots()[1]->uuid().value != 1);
    REQUIRE(first.children().size() == 1);
    CHECK(first.children()[0]->name() == "No id");
    CHECK_FALSE(first.children()[0]->uuid().isNull());
    CHECK(first.children()[0]->transform().localScale().x == 1.0f);
    CHECK(scene.roots()[2]->name() == "Lost child");
    CHECK(scene.roots()[2]->transform().localRotation().z == doctest::Approx(1.0f));
    CHECK(scene.roots()[3]->name() == "GameObject");
}

TEST_CASE("Components of unknown types are kept as they were")
{
    const std::string json = R"({"format":"arclight-scene","version":1,"objects":[
        {"id":"0000000000000001","name":"Lamp","components":[
            {"type":"Light","enabled":false,"intensity":2.5,"colour":[1,0.5,0]},
            {"enabled":true}
        ]}
    ]})";
    Scene scene;
    REQUIRE(readScene(scene, json, {}));
    const GameObject& lamp = *scene.roots()[0];
    REQUIRE(lamp.components().size() == 1);
    const Component& component = *lamp.components()[0];
    REQUIRE(&component.type() == &MissingComponent::staticType());
    const auto& missing = static_cast<const MissingComponent&>(component);
    CHECK(missing.typeName() == "Light");
    CHECK_FALSE(missing.enabled);
    CHECK(missing.data().getFloat("intensity") == 2.5f);

    const std::string saved = writeScene(scene);
    CHECK(saved.find("\"type\": \"Light\"") != std::string::npos);
    CHECK(saved.find("\"intensity\": 2.5") != std::string::npos);
    CHECK(saved.find("\"enabled\": false") != std::string::npos);

    // A registry that knows the type loads it as that type.
    ComponentRegistry registry;
    registry.add<MeshComponent>();
    CHECK(registry.contains("Mesh"));
    CHECK_FALSE(registry.contains("Light"));
    CHECK(registry.create("Mesh"));
    CHECK_FALSE(registry.create("Light"));
}

TEST_CASE("Mesh components are saved by model asset and keep it when it cannot load")
{
    auto device = rhi::createNullDevice({});
    REQUIRE(device);
    GeometryPool geometry(*device);
    ModelData data;
    data.name = "Quad";
    data.vertices.resize(3);
    data.indices = {0, 1, 2};
    Primitive primitive;
    primitive.indexCount = 3;
    primitive.vertexCount = 3;
    data.primitives = {primitive};
    data.meshes = {{.name = "Quad", .firstPrimitive = 0, .primitiveCount = 1}};
    const Uuid asset{0x1234};
    std::shared_ptr<const Model> model = Model::create(*device, geometry, data, asset);
    REQUIRE(model.get());

    Scene scene;
    GameObject& object = scene.create("Quad");
    object.addComponent<MeshComponent>(model, 0).enabled = false;
    CHECK(object.getComponent<MeshComponent>()->modelAsset() == asset);
    const std::string json = writeScene(scene);
    CHECK(json.find("\"model\": \"0000000000001234\"") != std::string::npos);

    Scene loaded;
    REQUIRE(readScene(loaded, json, {}));
    const MeshComponent* mesh = loaded.roots()[0]->getComponent<MeshComponent>();
    REQUIRE(mesh);
    CHECK_FALSE(mesh->enabled);
    CHECK_FALSE(mesh->model().get());
    CHECK(mesh->modelAsset() == asset);
    CHECK(writeScene(loaded) == json);
}

TEST_CASE("Loaded scenes get their models from the resource manager")
{
    VirtualFileSystem fileSystem;
    auto assets = std::make_shared<MemorySource>(true);
    assets->add("tri.gltf", test::triangleGltf());
    fileSystem.mount("assets", assets);
    fileSystem.mount("library", std::make_shared<MemorySource>(true));
    AssetDatabase database(fileSystem);
    database.refresh();
    auto device = rhi::createNullDevice({});
    REQUIRE(device);
    GeometryPool geometry(*device);
    ResourceManager resources(database, *device, geometry);

    const std::shared_ptr<const Model> model = resources.loadModel("assets://tri.gltf");
    REQUIRE(model.get());
    Scene scene;
    scene.create("Triangle").addComponent<MeshComponent>(model, 0);
    const std::string json = writeScene(scene);

    SceneLoadOptions options;
    options.context.resources = &resources;
    Scene loaded;
    REQUIRE(readScene(loaded, json, options));
    const MeshComponent* mesh = loaded.roots()[0]->getComponent<MeshComponent>();
    REQUIRE(mesh);
    CHECK(mesh->model().get() == model.get());
    CHECK(mesh->hasMesh());
    REQUIRE(resources.loaded().size() == 1);
    // This test, the original scene and the loaded scene.
    CHECK(resources.loaded()[0].references == 3);
}

TEST_CASE("Prefabs are placed with new ids, any number of times")
{
    Scene scene;
    buildSample(scene);
    GameObject& a = *scene.roots()[0];
    GameObject& b = *a.children()[0];
    const std::string prefab = writePrefab(b);
    CHECK(prefab.find("\"format\": \"arclight-prefab\"") != std::string::npos);
    // The prefab's root has no parent in the file.
    CHECK(prefab.find(a.uuid().toString()) == std::string::npos);

    GameObject* first = instantiatePrefab(scene, prefab, nullptr, {});
    GameObject* second = instantiatePrefab(scene, prefab, &a, {});
    REQUIRE(first);
    REQUIRE(second);
    CHECK(first->name() == "B");
    CHECK_FALSE(first->isActiveSelf());
    CHECK(first->parent() == nullptr);
    CHECK(second->parent() == &a);
    CHECK(first->uuid() != b.uuid());
    CHECK(second->uuid() != first->uuid());
    REQUIRE(first->children().size() == 1);
    CHECK(first->children()[0]->name() == "C");
    CHECK(first->children()[0]->uuid() != b.children()[0]->uuid());
    CHECK(first->transform().localPosition().z == -5.0f);
    CHECK(scene.objectCount() == 8);

    std::string error;
    CHECK_FALSE(instantiatePrefab(scene, writeScene(scene), nullptr, {}, &error));
    CHECK_FALSE(instantiatePrefab(scene, R"({"format":"arclight-prefab","version":1,"objects":[]})", nullptr, {}, &error));
    CHECK(error == "the prefab has no objects");
    CHECK_FALSE(readScene(scene, prefab, {}));
    CHECK(scene.objectCount() == 8);
}

TEST_CASE("writeFile() replaces native files")
{
    std::random_device random;
    const std::filesystem::path folder = std::filesystem::temp_directory_path() / ("arclight-scene-" + std::to_string(random()));
    const std::u8string u8 = (folder / "Scenes" / "Main.scene").generic_u8string();
    const std::string path(u8.begin(), u8.end());
    const std::string_view first = "first";
    const std::string_view second = "second";
    CHECK(writeFile(path, std::as_bytes(std::span(first.data(), first.size()))));
    CHECK(writeFile(path, std::as_bytes(std::span(second.data(), second.size()))));
    const std::optional<std::vector<std::byte>> bytes = readFile(path);
    REQUIRE(bytes);
    CHECK(std::string(reinterpret_cast<const char*>(bytes->data()), bytes->size()) == "second");
    std::error_code ignored;
    std::filesystem::remove_all(folder, ignored);
}

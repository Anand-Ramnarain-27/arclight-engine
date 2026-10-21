#include "RHI/Backends.h"
#include "RHI/Device.h"
#include "Renderer/Model.h"
#include "Scene/MeshComponent.h"
#include "Scene/ModelInstantiation.h"
#include "Scene/Scene.h"

#include <doctest/doctest.h>

#include <memory>
#include <unordered_set>
#include <vector>

using namespace arc;

namespace
{
    bool near(const Vec3& a, const Vec3& b) { return nearlyEqual(a, b, 1e-4f); }

    class CounterComponent final : public Component
    {
        ARC_COMPONENT(CounterComponent, "Counter")

    public:
        void update(const FrameTime& /*time*/) override { ++updates; }

        int updates = 0;
    };

    std::vector<std::string> names(std::span<GameObject* const> objects)
    {
        std::vector<std::string> result;
        for (const GameObject* object : objects)
            result.push_back(object->name());
        return result;
    }
}

TEST_CASE("UUIDs are unique, non-null and round-trip through text")
{
    std::unordered_set<Uuid> seen;
    for (int i = 0; i < 1000; ++i)
    {
        const Uuid uuid = Uuid::generate();
        CHECK(uuid);
        CHECK(seen.insert(uuid).second);
    }

    const Uuid uuid{0x0123456789abcdefull};
    CHECK(uuid.toString() == "0123456789abcdef");
    CHECK(Uuid::fromString("0123456789ABCDEF") == uuid);
    CHECK_FALSE(Uuid::fromString("0123456789abcde"));
    CHECK_FALSE(Uuid::fromString("0123456789abcdeg"));
}

TEST_CASE("decompose() inverts Mat4::trs(), including mirrors")
{
    const Quat rotation = Quat::fromEuler({0.4f, -2.1f, 0.9f});
    const auto trs = decompose(Mat4::trs({1, 2, 3}, rotation, {2, 3, 4}));
    REQUIRE(trs);
    CHECK(near(trs->translation, {1, 2, 3}));
    CHECK(near(trs->scale, {2, 3, 4}));
    CHECK(nearlyEqualRotation(trs->rotation, rotation, 1e-4f));

    // A mirror is reported as negative X scale; whichever axis it was on, the matrix is the same.
    const Mat4 mirrored = Mat4::trs({0, 0, 0}, rotation, {1, -1, 1});
    const auto mirror = decompose(mirrored);
    REQUIRE(mirror);
    CHECK(mirror->scale.x < 0.0f);
    CHECK(nearlyEqual(Mat4::trs(mirror->translation, mirror->rotation, mirror->scale), mirrored, 1e-4f));

    CHECK_FALSE(decompose(Mat4::scaling({1, 0, 1})));

    // Rotations near 180 degrees take the other branches of the matrix-to-quaternion conversion.
    for (const Vec3& axis : {Vec3{1, 0, 0}, Vec3{0, 1, 0}, Vec3{0, 0, 1}, Vec3{1, 1, 0}})
    {
        const Quat q = Quat::fromAxisAngle(axis, kPi * 0.999f);
        CHECK(nearlyEqualRotation(Quat::fromRotationMatrix(Mat4::rotation(q)), q, 1e-4f));
    }
}

TEST_CASE("World matrices follow the hierarchy and update when an ancestor moves")
{
    Scene scene;
    GameObject& parent = scene.create("Parent");
    GameObject& child = scene.create("Child", &parent);
    GameObject& grandchild = scene.create("Grandchild", &child);

    parent.transform().setLocalPosition({10, 0, 0});
    child.transform().setLocalScale(Vec3{2});
    grandchild.transform().setLocalPosition({1, 0, 0});
    CHECK(near(grandchild.transform().worldPosition(), {12, 0, 0}));

    // The grandchild's cached matrix must be invalidated through the child.
    parent.transform().setLocalRotation(Quat::fromAxisAngle(Vec3::up(), kHalfPi));
    CHECK(near(grandchild.transform().worldPosition(), {10, 0, -2}));

    CHECK(parent.parent() == nullptr);
    CHECK(child.parent() == &parent);
    CHECK(grandchild.isDescendantOf(parent));
    CHECK_FALSE(parent.isDescendantOf(parent));
    CHECK(names(scene.roots()) == std::vector<std::string>{"Parent"});
    CHECK(scene.objectCount() == 3);
}

TEST_CASE("Reparenting keeps the world transform and rejects cycles")
{
    Scene scene;
    GameObject& a = scene.create("A");
    GameObject& b = scene.create("B");
    GameObject& c = scene.create("C", &a);

    a.transform().setLocal({5, 0, 0}, Quat::fromAxisAngle(Vec3::up(), 0.7f), Vec3{2});
    c.transform().setLocalPosition({1, 1, 0});
    const Mat4 world = c.transform().worldMatrix();

    CHECK(c.setParent(&b));
    CHECK(c.parent() == &b);
    CHECK(nearlyEqual(c.transform().worldMatrix(), world, 1e-4f));

    // Without keeping the world transform, the local values stay and the object moves with B.
    b.transform().setLocalPosition({0, 3, 0});
    const Vec3 local = c.transform().localPosition();
    CHECK(c.setParent(&a, false));
    CHECK(near(c.transform().localPosition(), local));
    CHECK(near(c.transform().worldPosition(), a.transform().worldMatrix().transformPoint(local)));
    CHECK(c.setParent(nullptr));
    CHECK(c.parent() == nullptr);
    CHECK(nearlyEqual(c.transform().worldMatrix(), c.transform().localMatrix(), 1e-4f));

    CHECK(c.setParent(&b));
    CHECK_FALSE(b.setParent(&c));
    CHECK_FALSE(b.setParent(&b));
    CHECK(b.parent() == nullptr);
}

TEST_CASE("Sibling order can be changed by index")
{
    Scene scene;
    GameObject& a = scene.create("A");
    scene.create("B");
    GameObject& c = scene.create("C");

    CHECK(c.setParent(nullptr, true, 0));
    CHECK(names(scene.roots()) == std::vector<std::string>{"C", "A", "B"});

    // Within the same parent the index counts positions with the object already taken out, so
    // "insert before the object at index 3" means the end.
    CHECK(c.setParent(nullptr, true, 3));
    CHECK(names(scene.roots()) == std::vector<std::string>{"A", "B", "C"});
    CHECK(a.setParent(nullptr, true, 2));
    CHECK(names(scene.roots()) == std::vector<std::string>{"B", "A", "C"});
    CHECK(a.siblingIndex() == 1);
}

TEST_CASE("Destruction is deferred and leaves handles stale")
{
    Scene scene;
    GameObject& parent = scene.create("Parent");
    GameObject& child = scene.create("Child", &parent);
    GameObject& other = scene.create("Other");
    const GameObjectHandle parentHandle = parent.handle();
    const GameObjectHandle childHandle = child.handle();

    scene.destroy(parent);
    CHECK(scene.find(parentHandle) == nullptr);
    CHECK(scene.find(childHandle) == nullptr);
    CHECK(child.name() == "Child");   // Still alive until the flush.
    CHECK(scene.objectCount() == 3);

    scene.destroy(child);   // Already marked through its parent.
    scene.flushDestroyed();
    CHECK(scene.objectCount() == 1);
    CHECK(names(scene.roots()) == std::vector<std::string>{"Other"});
    CHECK(scene.find(other.handle()) == &other);

    // The freed slots are reused, but the old handles stay stale.
    GameObject& reused = scene.create("Reused");
    CHECK(scene.find(parentHandle) == nullptr);
    CHECK(scene.find(childHandle) == nullptr);
    CHECK(scene.find(reused.handle()) == &reused);
    CHECK(scene.findByUuid(reused.uuid()) == &reused);
}

TEST_CASE("Duplicates are deep copies with new identities")
{
    Scene scene;
    GameObject& original = scene.create("Original");
    original.transform().setLocalPosition({1, 2, 3});
    original.addComponent<CounterComponent>().updates = 7;
    GameObject& child = scene.create("Child", &original);
    child.setActive(false);
    scene.create("After");

    GameObject& copy = scene.duplicate(original);
    CHECK(names(scene.roots()) == std::vector<std::string>{"Original", "Original", "After"});
    CHECK(copy.siblingIndex() == 1);
    CHECK(copy.uuid() != original.uuid());
    CHECK(near(copy.transform().localPosition(), {1, 2, 3}));

    CounterComponent* counter = copy.getComponent<CounterComponent>();
    REQUIRE(counter != nullptr);
    CHECK(counter != original.getComponent<CounterComponent>());
    CHECK(counter->updates == 7);
    CHECK(&counter->owner() == &copy);

    REQUIRE(copy.children().size() == 1);
    CHECK(copy.children()[0]->name() == "Child");
    CHECK_FALSE(copy.children()[0]->isActiveSelf());
    CHECK(copy.children()[0]->uuid() != child.uuid());
}

TEST_CASE("Only enabled components of active objects are updated")
{
    Scene scene;
    GameObject& parent = scene.create("Parent");
    GameObject& child = scene.create("Child", &parent);
    auto& parentCounter = parent.addComponent<CounterComponent>();
    auto& childCounter = child.addComponent<CounterComponent>();

    scene.update({});
    CHECK(parentCounter.updates == 1);
    CHECK(childCounter.updates == 1);

    parent.setActive(false);
    scene.update({});
    CHECK(parentCounter.updates == 1);
    CHECK(childCounter.updates == 1);
    CHECK(child.isActiveSelf());
    CHECK_FALSE(child.isActiveInHierarchy());

    parent.setActive(true);
    childCounter.enabled = false;
    scene.update({});
    CHECK(parentCounter.updates == 2);
    CHECK(childCounter.updates == 1);

    CHECK(parent.removeComponent(parentCounter));
    CHECK(parent.getComponent<CounterComponent>() == nullptr);
}

TEST_CASE("A model's nodes become GameObjects with mesh components")
{
    ModelData data;
    data.name = "Sample";
    data.vertices = {{{0, 0, 0}, {0, 0, 1}, {}}, {{1, 0, 0}, {0, 0, 1}, {}}, {{0, 1, 0}, {0, 0, 1}, {}}};
    data.indices = {0, 1, 2};
    Primitive primitive;
    primitive.indexCount = 3;
    primitive.vertexCount = 3;
    for (const MeshVertex& vertex : data.vertices)
        primitive.bounds.expand(vertex.position);
    data.primitives = {primitive};
    data.meshes = {{.name = "Triangle", .firstPrimitive = 0, .primitiveCount = 1}};
    data.instances = {{.name = "Mesh", .world = Mat4::translation({5, 0, 0}), .mesh = 0}};
    data.nodes.resize(3);
    data.nodes[0].name = "Pivot";
    data.nodes[0].translation = {5, 0, 0};
    data.nodes[1].name = "Mesh";
    data.nodes[1].parent = 0;
    data.nodes[1].mesh = 0;
    data.nodes[2].parent = 0;

    auto device = rhi::createNullDevice({});
    REQUIRE(device);
    GeometryPool geometry(*device);
    std::shared_ptr<const Model> model = Model::create(*device, geometry, data);
    REQUIRE(model);

    Scene scene;
    GameObject& root = instantiateModel(scene, model);
    CHECK(root.name() == "Sample");
    REQUIRE(root.children().size() == 1);
    GameObject& pivot = *root.children()[0];
    CHECK(names(pivot.children()) == std::vector<std::string>{"Mesh", "Node 2"});

    const MeshComponent* mesh = pivot.children()[0]->getComponent<MeshComponent>();
    REQUIRE(mesh != nullptr);
    CHECK(mesh->hasMesh());
    CHECK(mesh->model() == model);
    CHECK(pivot.children()[1]->getComponent<MeshComponent>() == nullptr);

    root.transform().setLocalPosition({0, 10, 0});
    const Aabb bounds = hierarchyBounds(root);
    CHECK(near(bounds.min, {5, 10, 0}));
    CHECK(near(bounds.max, {6, 11, 0}));

    // Every MeshComponent shares the model: it lives until the last one is gone.
    const std::weak_ptr<const Model> weak = model;
    model.reset();
    CHECK_FALSE(weak.expired());
    scene.destroy(root);
    scene.flushDestroyed();
    CHECK(weak.expired());
}

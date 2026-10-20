#include "Math/Ray.h"
#include "RHI/Backends.h"
#include "RHI/Device.h"
#include "Renderer/Camera.h"
#include "Renderer/Model.h"
#include "Scene/MeshComponent.h"
#include "Scene/Raycast.h"
#include "Scene/Scene.h"

#include <doctest/doctest.h>

#include <memory>

using namespace arc;

namespace
{
    bool near(float a, float b) { return nearlyEqual(a, b, 1e-4f); }
    bool near(const Vec3& a, const Vec3& b) { return nearlyEqual(a, b, 1e-4f); }

    // A unit square in the XY plane, centred on the origin, facing +Z: two triangles.
    std::shared_ptr<const Model> makeSquare(rhi::Device& device)
    {
        ModelData data;
        data.name = "Square";
        data.vertices = {{{-0.5f, -0.5f, 0}, {0, 0, 1}, {}},
                         {{0.5f, -0.5f, 0}, {0, 0, 1}, {}},
                         {{0.5f, 0.5f, 0}, {0, 0, 1}, {}},
                         {{-0.5f, 0.5f, 0}, {0, 0, 1}, {}}};
        data.indices = {0, 1, 2, 0, 2, 3};
        Primitive primitive;
        primitive.indexCount = 6;
        primitive.vertexCount = 4;
        for (const MeshVertex& vertex : data.vertices)
            primitive.bounds.expand(vertex.position);
        data.primitives = {primitive};
        data.meshes = {{.name = "Square", .firstPrimitive = 0, .primitiveCount = 1}};
        return Model::create(device, data);
    }
}

TEST_CASE("Rays against boxes")
{
    const Aabb box{{-1, -1, -1}, {1, 1, 1}};

    const auto front = intersectRayAabb({{0, 0, 5}, {0, 0, -1}}, box);
    REQUIRE(front);
    CHECK(near(*front, 4.0f));

    const auto inside = intersectRayAabb({{0, 0, 0}, {1, 0, 0}}, box);
    REQUIRE(inside);
    CHECK(*inside == 0.0f);

    CHECK_FALSE(intersectRayAabb({{0, 0, 5}, {0, 0, 1}}, box));        // Pointing away.
    CHECK_FALSE(intersectRayAabb({{0, 2, 5}, {0, 0, -1}}, box));       // Parallel, outside the Y slab.
    CHECK(intersectRayAabb({{0, 0.5f, 5}, {0, 0, -1}}, box));          // Parallel, inside it.
    CHECK_FALSE(intersectRayAabb({{0, 0, 5}, {0, 0, -1}}, Aabb{}));    // Empty box.

    // Distances are in multiples of the direction.
    const auto scaled = intersectRayAabb({{0, 0, 5}, {0, 0, -2}}, box);
    REQUIRE(scaled);
    CHECK(near(*scaled, 2.0f));
}

TEST_CASE("Rays against triangles hit from both sides")
{
    const Vec3 a{-1, -1, 0}, b{1, -1, 0}, c{0, 1, 0};

    const auto front = intersectRayTriangle({{0, 0, 3}, {0, 0, -1}}, a, b, c);
    REQUIRE(front);
    CHECK(near(*front, 3.0f));
    const auto back = intersectRayTriangle({{0, 0, -2}, {0, 0, 1}}, a, b, c);
    REQUIRE(back);
    CHECK(near(*back, 2.0f));

    CHECK_FALSE(intersectRayTriangle({{2, 0, 3}, {0, 0, -1}}, a, b, c));          // Beside it.
    CHECK_FALSE(intersectRayTriangle({{0, 0, 3}, {0, 0, 1}}, a, b, c));           // Behind the origin.
    CHECK_FALSE(intersectRayTriangle({{0, 0, 3}, {1, 0, 0}}, a, b, c));           // In the plane's direction.
    CHECK_FALSE(intersectRayTriangle({{0, 0, 3}, {0, 0, -1}}, a, b, a + (b - a) * 2.0f));   // Degenerate.
}

TEST_CASE("Viewport rays go through the image")
{
    Camera camera;
    camera.position = {1, 2, 3};
    camera.yaw = 0.7f;
    camera.pitch = -0.2f;

    const Ray centre = camera.viewportRay({0, 0}, 16.0f / 9.0f);
    CHECK(near(centre.origin, camera.position));
    CHECK(near(centre.direction, camera.forward()));

    // The top edge of the image is half the field of view above the view direction.
    const Ray top = camera.viewportRay({0, 1}, 16.0f / 9.0f);
    CHECK(near(std::acos(dot(top.direction, camera.forward())), camera.verticalFov * 0.5f));
    CHECK(dot(top.direction, camera.up()) > 0.0f);

    // Projecting a point along a ray lands back on the same NDC position, in either depth convention.
    for (const bool reversedZ : {false, true})
    {
        camera.reversedZ = reversedZ;
        const float aspect = 1.5f;
        const Vec2 ndc{0.3f, -0.6f};
        const Vec4 clip = camera.projection(aspect) * camera.view() * Vec4(camera.viewportRay(ndc, aspect).at(10.0f), 1.0f);
        CHECK(near(clip.x / clip.w, ndc.x));
        CHECK(near(clip.y / clip.w, ndc.y));
    }
}

TEST_CASE("Scene raycasts find the nearest mesh, in world units")
{
    auto device = rhi::createNullDevice({});
    REQUIRE(device);
    const std::shared_ptr<const Model> square = makeSquare(*device);
    REQUIRE(square);

    Scene scene;
    GameObject& nearSquare = scene.create("Near");
    nearSquare.addComponent<MeshComponent>(square, 0);
    nearSquare.transform().setLocalPosition({0, 0, -2});
    GameObject& farSquare = scene.create("Far");
    farSquare.addComponent<MeshComponent>(square, 0);
    farSquare.transform().setLocal({0, 0, -6}, Quat::fromAxisAngle(Vec3::up(), 0.5f), Vec3{4});
    scene.create("Empty");

    const Ray ray{{0, 0, 0}, {0, 0, -1}};
    auto hit = raycast(scene, ray);
    REQUIRE(hit);
    CHECK(hit->object == &nearSquare);
    CHECK(near(hit->distance, 2.0f));
    CHECK(near(hit->point, {0, 0, -2}));

    // The far square is rotated and scaled by 4; its distance is still measured in the world.
    nearSquare.setActive(false);
    hit = raycast(scene, ray);
    REQUIRE(hit);
    CHECK(hit->object == &farSquare);
    CHECK(near(hit->distance, 6.0f));

    // Disabled meshes and rays that pass beside everything hit nothing.
    farSquare.getComponent<MeshComponent>()->enabled = false;
    CHECK_FALSE(raycast(scene, ray));
    farSquare.getComponent<MeshComponent>()->enabled = true;
    CHECK_FALSE(raycast(scene, {{10, 0, 0}, {0, 0, -1}}));

    // Zero scale hides an object from picking as it does from view.
    farSquare.transform().setLocalScale(Vec3::zero());
    CHECK_FALSE(raycast(scene, ray));
}

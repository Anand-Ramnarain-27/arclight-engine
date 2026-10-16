#include "Renderer/Camera.h"

#include <doctest/doctest.h>

using namespace arc;

namespace
{
    // NDC depth of a point straight ahead of the camera at `distance`.
    float depthAt(const Camera& camera, float distance)
    {
        const Vec4 clip = camera.projection(16.0f / 9.0f) * camera.view() *
                          Vec4(camera.position + camera.forward() * distance, 1.0f);
        return clip.z / clip.w;
    }
}

TEST_CASE("Camera orientation follows yaw and pitch")
{
    Camera camera;
    CHECK(nearlyEqual(camera.forward(), Vec3::forward()));

    camera.yaw = toRadians(90.0f);   // Positive yaw turns left, toward -X.
    CHECK(nearlyEqual(camera.forward(), Vec3{-1.0f, 0.0f, 0.0f}));

    camera.yaw = 0.0f;
    camera.pitch = toRadians(30.0f);
    CHECK(camera.forward().y > 0.0f);
    CHECK(nearlyEqual(dot(camera.forward(), camera.right()), 0.0f));
    CHECK(nearlyEqual(camera.right().y, 0.0f));   // Pitch never rolls the horizon.
}

TEST_CASE("Camera::lookAlong inverts forward()")
{
    Camera camera;
    const Vec3 direction = normalize(Vec3{-3.0f, -2.0f, 5.0f});
    camera.lookAlong(direction * 4.0f);
    CHECK(nearlyEqual(camera.forward(), direction, 1e-5f));

    // Straight down is clamped short of the pole, so yaw stays meaningful.
    camera.lookAlong({0.0f, -1.0f, 0.0f});
    CHECK(nearlyEqual(camera.pitch, -Camera::kMaxPitch));
}

TEST_CASE("Camera::view matches lookAt")
{
    Camera camera;
    camera.position = {3.0f, 2.0f, 5.0f};
    camera.lookAlong(-camera.position);
    CHECK(nearlyEqual(camera.view(), Mat4::lookAt(camera.position, Vec3::zero(), Vec3::up()), 1e-5f));
    CHECK(nearlyEqual(camera.view().transformPoint(camera.position), Vec3::zero(), 1e-5f));
}

TEST_CASE("Standard and reversed depth")
{
    Camera camera;
    camera.nearZ = 0.1f;
    camera.farZ = 1000.0f;

    camera.reversedZ = false;
    CHECK(nearlyEqual(depthAt(camera, camera.nearZ), 0.0f, 1e-5f));
    CHECK(nearlyEqual(depthAt(camera, camera.farZ), 1.0f, 1e-5f));
    CHECK(depthAt(camera, 10.0f) < depthAt(camera, 20.0f));
    CHECK(camera.clearDepth() == 1.0f);
    CHECK(camera.depthCompare() == rhi::CompareOp::LessEqual);

    // Reversed: the near plane at 1, decreasing toward 0 at infinity, with no far plane.
    camera.reversedZ = true;
    CHECK(nearlyEqual(depthAt(camera, camera.nearZ), 1.0f, 1e-5f));
    CHECK(depthAt(camera, 10.0f) > depthAt(camera, 20.0f));
    CHECK(depthAt(camera, 1.0e6f) > 0.0f);
    CHECK(depthAt(camera, 1.0e6f) < 1.0e-6f);
    CHECK(camera.clearDepth() == 0.0f);
    CHECK(camera.depthCompare() == rhi::CompareOp::GreaterEqual);
}

TEST_CASE("Reversed-Z keeps distant surfaces apart in a 32-bit float depth buffer")
{
    // Two surfaces 1 cm apart, 500 m away: the depth values must differ to be told apart.
    Camera camera;
    camera.nearZ = 0.01f;
    camera.farZ = 1000.0f;

    camera.reversedZ = false;
    const bool standardSeparates = depthAt(camera, 500.0f) != depthAt(camera, 500.01f);
    camera.reversedZ = true;
    const bool reversedSeparates = depthAt(camera, 500.0f) != depthAt(camera, 500.01f);

    CHECK_FALSE(standardSeparates);
    CHECK(reversedSeparates);
}

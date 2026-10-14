#include "Math/Math.h"

#include <doctest/doctest.h>

using namespace arc;

namespace
{
    bool isIdentity(const Mat4& m) { return nearlyEqual(m, Mat4::identity()); }
}

TEST_CASE("Vec3 basics")
{
    CHECK(dot(Vec3{1, 2, 3}, Vec3{4, 5, 6}) == 32.0f);
    CHECK(length(Vec3{3, 4, 0}) == 5.0f);
    CHECK(nearlyEqual(length(normalize(Vec3{1, 2, 3})), 1.0f));
    CHECK(normalize(Vec3::zero()) == Vec3::zero());
}

TEST_CASE("Right-handed basis")
{
    CHECK(cross(Vec3::right(), Vec3::up()) == Vec3{0, 0, 1});
    CHECK(cross(Vec3::right(), Vec3::up()) == -Vec3::forward());
}

TEST_CASE("Mat4 storage is column-major")
{
    const Mat4 t = Mat4::translation({1, 2, 3});
    CHECK(t(0, 3) == 1.0f);
    CHECK(t(1, 3) == 2.0f);
    CHECK(t(2, 3) == 3.0f);
    CHECK(t.data()[12] == 1.0f);
    CHECK(t.data()[13] == 2.0f);
    CHECK(t.data()[14] == 3.0f);
}

TEST_CASE("TRS applies scale, then rotation, then translation")
{
    const Quat quarterTurnY = Quat::fromAxisAngle(Vec3::up(), kHalfPi);
    const Mat4 m = Mat4::trs({10, 0, 0}, quarterTurnY, Vec3{2});

    // (1,0,0) scaled to (2,0,0), rotated +90 degrees about Y to (0,0,-2), moved to (10,0,-2).
    CHECK(nearlyEqual(m.transformPoint({1, 0, 0}), Vec3{10, 0, -2}));
    CHECK(nearlyEqual(m.transformVector({1, 0, 0}), Vec3{0, 0, -2}));

    const Mat4 composed = Mat4::translation({10, 0, 0}) * Mat4::rotation(quarterTurnY) * Mat4::scaling(Vec3{2});
    CHECK(nearlyEqual(m, composed));
}

TEST_CASE("Inverse and determinant")
{
    const Mat4 m = Mat4::trs({1, -2, 3}, Quat::fromEuler({0.3f, -1.2f, 0.7f}), {2, 3, 0.5f});
    CHECK(isIdentity(m * inverse(m)));
    CHECK(isIdentity(inverse(m) * m));
    CHECK(nearlyEqual(determinant(m), 3.0f, 1e-4f));
    CHECK(nearlyEqual(transpose(transpose(m)), m));
}

TEST_CASE("Normal matrix keeps normals perpendicular under non-uniform scale")
{
    const Mat4 model = Mat4::scaling({4, 1, 1});
    const Vec3 tangent = normalize(Vec3{1, 1, 0});
    const Vec3 normal = normalize(Vec3{-1, 1, 0});

    const Vec3 t = model.transformVector(tangent);
    const Vec3 wrong = model.transformVector(normal);
    const Vec3 right = normalMatrix(model).transformVector(normal);

    CHECK_FALSE(nearlyEqual(dot(t, wrong), 0.0f));
    CHECK(nearlyEqual(dot(t, right), 0.0f));
}

TEST_CASE("Quaternion rotation matches the matrix")
{
    const Quat q = Quat::fromAxisAngle(normalize(Vec3{1, 2, 3}), 0.8f);
    const Vec3 v{0.3f, -1.5f, 2.0f};
    CHECK(nearlyEqual(q.rotate(v), Mat4::rotation(q).transformVector(v)));
}

TEST_CASE("Quaternion composition order")
{
    const Quat yaw = Quat::fromAxisAngle(Vec3::up(), kHalfPi);
    const Quat pitch = Quat::fromAxisAngle(Vec3::right(), kHalfPi);
    const Vec3 v{0, 0, -1};
    CHECK(nearlyEqual((yaw * pitch).rotate(v), yaw.rotate(pitch.rotate(v))));
    CHECK(nearlyEqual(Mat4::rotation(yaw * pitch), Mat4::rotation(yaw) * Mat4::rotation(pitch)));
}

TEST_CASE("Positive yaw turns the forward vector to the left")
{
    const Quat yaw = Quat::fromEuler({0.0f, kHalfPi, 0.0f});
    CHECK(nearlyEqual(yaw.rotate(Vec3::forward()), Vec3{-1, 0, 0}));
}

TEST_CASE("Euler round trip")
{
    const Vec3 angles{0.4f, -2.1f, 1.1f};
    const Vec3 back = Quat::fromEuler(angles).toEuler();
    CHECK(nearlyEqual(back, angles, 1e-4f));
    CHECK(nearlyEqualRotation(Quat::fromEuler(back), Quat::fromEuler(angles)));
}

TEST_CASE("Euler at gimbal lock still reproduces the rotation")
{
    const Vec3 angles{kHalfPi, 0.6f, 0.3f};
    const Vec3 back = Quat::fromEuler(angles).toEuler();
    CHECK(back.z == 0.0f);
    CHECK(nearlyEqualRotation(Quat::fromEuler(back), Quat::fromEuler(angles), 1e-4f));
}

TEST_CASE("Slerp")
{
    const Quat a = Quat::identity();
    const Quat b = Quat::fromAxisAngle(Vec3::up(), kHalfPi);
    CHECK(nearlyEqualRotation(slerp(a, b, 0.0f), a));
    CHECK(nearlyEqualRotation(slerp(a, b, 1.0f), b));
    CHECK(nearlyEqualRotation(slerp(a, b, 0.5f), Quat::fromAxisAngle(Vec3::up(), kHalfPi * 0.5f)));

    // -b is the same rotation; slerp must still take the short path.
    const Quat negB{-b.x, -b.y, -b.z, -b.w};
    CHECK(nearlyEqualRotation(slerp(a, negB, 0.5f), Quat::fromAxisAngle(Vec3::up(), kHalfPi * 0.5f)));
}

TEST_CASE("lookAt maps the eye to the origin and the target onto -Z")
{
    const Vec3 eye{3, 4, 5};
    const Vec3 target{0, 1, 0};
    const Mat4 view = Mat4::lookAt(eye, target, Vec3::up());

    CHECK(nearlyEqual(view.transformPoint(eye), Vec3::zero()));
    const Vec3 t = view.transformPoint(target);
    CHECK(nearlyEqual(t.x, 0.0f));
    CHECK(nearlyEqual(t.y, 0.0f));
    CHECK(t.z < 0.0f);
}

TEST_CASE("Perspective depth range is [0, 1]")
{
    const Mat4 proj = Mat4::perspective(toRadians(60.0f), 16.0f / 9.0f, 0.1f, 100.0f);
    const Vec4 nearClip = proj * Vec4(0, 0, -0.1f, 1);
    const Vec4 farClip = proj * Vec4(0, 0, -100.0f, 1);
    CHECK(nearlyEqual(nearClip.z / nearClip.w, 0.0f));
    CHECK(nearlyEqual(farClip.z / farClip.w, 1.0f));
}

TEST_CASE("Reversed-Z puts the near plane at 1 and infinity toward 0")
{
    const Mat4 proj = Mat4::perspectiveReversedZ(toRadians(60.0f), 1.0f, 0.1f);
    const Vec4 nearClip = proj * Vec4(0, 0, -0.1f, 1);
    const Vec4 farClip = proj * Vec4(0, 0, -1.0e6f, 1);
    CHECK(nearlyEqual(nearClip.z / nearClip.w, 1.0f));
    CHECK(farClip.z / farClip.w < 1e-6f);
}

TEST_CASE("Orthographic maps the box to NDC")
{
    const Mat4 proj = Mat4::orthographic(-2, 2, -1, 1, 1, 11);
    CHECK(nearlyEqual(proj.transformPoint({-2, -1, -1}), Vec3{-1, -1, 0}));
    CHECK(nearlyEqual(proj.transformPoint({2, 1, -11}), Vec3{1, 1, 1}));
}

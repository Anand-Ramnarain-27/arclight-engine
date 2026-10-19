#pragma once

#include "Math/Vector.h"

namespace arc
{
    class Mat4;

    /// Unit quaternion representing a rotation. (x, y, z) is the vector part, w the scalar part.
    struct Quat
    {
        float x = 0.0f;
        float y = 0.0f;
        float z = 0.0f;
        float w = 1.0f;

        constexpr Quat() = default;
        constexpr Quat(float x_, float y_, float z_, float w_) : x(x_), y(y_), z(z_), w(w_) {}

        static constexpr Quat identity() { return {}; }

        /// Counter-clockwise rotation by `radians` around `axis` when looking down the axis toward the origin.
        [[nodiscard]] static Quat fromAxisAngle(const Vec3& axis, float radians);

        /// Radians. Applied as yaw around Y, then pitch around the rotated X, then roll around
        /// the rotated Z (intrinsic Y-X-Z), which keeps the horizon level for cameras.
        [[nodiscard]] static Quat fromEuler(const Vec3& pitchYawRoll);

        /// The rotation of a matrix whose upper 3x3 is orthonormal with determinant +1 (no scale,
        /// no mirroring). Translation is ignored.
        [[nodiscard]] static Quat fromRotationMatrix(const Mat4& m);

        /// Inverse of fromEuler. Pitch is in [-pi/2, pi/2]; at +-pi/2 roll is reported as 0.
        [[nodiscard]] Vec3 toEuler() const;

        /// Hamilton product. (a * b) rotates by b first, then by a, like matrices.
        constexpr Quat operator*(const Quat& o) const
        {
            return {
                w * o.x + x * o.w + y * o.z - z * o.y,
                w * o.y - x * o.z + y * o.w + z * o.x,
                w * o.z + x * o.y - y * o.x + z * o.w,
                w * o.w - x * o.x - y * o.y - z * o.z,
            };
        }

        /// Rotates a vector.
        [[nodiscard]] constexpr Vec3 rotate(const Vec3& v) const
        {
            const Vec3 u{x, y, z};
            const Vec3 t = cross(u, v) * 2.0f;
            return v + t * w + cross(u, t);
        }

        constexpr bool operator==(const Quat&) const = default;
    };

    [[nodiscard]] constexpr float dot(const Quat& a, const Quat& b) { return a.x * b.x + a.y * b.y + a.z * b.z + a.w * b.w; }

    /// For a unit quaternion this is also the inverse.
    [[nodiscard]] constexpr Quat conjugate(const Quat& q) { return {-q.x, -q.y, -q.z, q.w}; }

    [[nodiscard]] Quat normalize(const Quat& q);

    /// Constant angular velocity interpolation along the shortest arc.
    [[nodiscard]] Quat slerp(const Quat& a, const Quat& b, float t);

    /// q and -q are the same rotation, so both compare equal.
    [[nodiscard]] bool nearlyEqualRotation(const Quat& a, const Quat& b, float epsilon = kEpsilon);
}

#pragma once

#include "Math/Aabb.h"
#include "Math/Matrix.h"
#include "Math/Vector.h"

#include <array>
#include <cstdint>

namespace arc
{
    /// The points p with dot(normal, p) + distance >= 0 are on its positive side. `normal` is unit
    /// length, so the expression is the signed distance to the plane.
    struct Plane
    {
        Vec3 normal{0.0f, 1.0f, 0.0f};
        float distance = 0.0f;

        [[nodiscard]] static Plane fromPointNormal(const Vec3& point, const Vec3& normal)
        {
            const Vec3 n = normalize(normal);
            return {n, -dot(n, point)};
        }

        [[nodiscard]] float signedDistance(const Vec3& point) const { return dot(normal, point) + distance; }
    };

    /// A box of any orientation: a centre and three half-axes, each scaled by its half extent. An
    /// AABB under any affine transform (rotation, non-uniform scale, even shear) is one exactly.
    struct Obb
    {
        Vec3 center;
        std::array<Vec3, 3> halfAxes{};

        [[nodiscard]] static Obb fromAabb(const Aabb& box, const Mat4& transform)
        {
            const Vec3 halfSize = box.size() * 0.5f;
            return {transform.transformPoint(box.center()),
                    {transform.transformVector({halfSize.x, 0.0f, 0.0f}), transform.transformVector({0.0f, halfSize.y, 0.0f}),
                     transform.transformVector({0.0f, 0.0f, halfSize.z})}};
        }
    };

    enum class Containment : uint8_t
    {
        Outside,
        Intersects,
        Inside,
    };

    /// The volume a camera sees, as planes facing inwards: left, right, bottom, top, near and,
    /// unless the projection reaches infinity, far.
    struct Frustum
    {
        std::array<Plane, 6> planes{};
        uint32_t planeCount = 0;

        /// `nearZ` and `farZ` are distances along `forward`; a far distance of infinity leaves the far
        /// plane out. `verticalFov` is in radians, `aspect` is width / height.
        [[nodiscard]] static Frustum fromCamera(const Vec3& position, const Vec3& forward, const Vec3& right,
                                                const Vec3& up, float verticalFov, float aspect, float nearZ, float farZ);

        /// Conservative: Intersects may be reported for a box that is outside but near a corner of
        /// the frustum, never Outside for one that is partly inside.
        [[nodiscard]] Containment classify(const Aabb& box) const;
        [[nodiscard]] Containment classify(const Obb& box) const;
    };
}

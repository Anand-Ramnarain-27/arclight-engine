#pragma once

#include "Math/Aabb.h"
#include "Math/Vector.h"

#include <optional>

namespace arc
{
    /// A half-line from `origin` along `direction`. The direction need not be unit length: distances
    /// along the ray are in multiples of it, which keeps them meaningful after an affine transform.
    struct Ray
    {
        Vec3 origin;
        Vec3 direction{0.0f, 0.0f, -1.0f};

        [[nodiscard]] constexpr Vec3 at(float t) const { return origin + direction * t; }

        /// The same ray in the space `transform` maps into. t values carry over unchanged, because
        /// both the origin and the direction are transformed (the direction without translation).
        [[nodiscard]] Ray transformed(const Mat4& transform) const
        {
            return {transform.transformPoint(origin), transform.transformVector(direction)};
        }
    };

    /// The t at which the ray enters the box, 0 if it starts inside, or nothing if it misses or the
    /// box is entirely behind it. Slab method; rays parallel to a slab are handled by IEEE infinities.
    [[nodiscard]] std::optional<float> intersectRayAabb(const Ray& ray, const Aabb& box);

    /// The t of the hit with triangle (a, b, c), from either side, or nothing for a miss, a hit
    /// behind the origin, or a degenerate triangle. Moller-Trumbore.
    [[nodiscard]] std::optional<float> intersectRayTriangle(const Ray& ray, const Vec3& a, const Vec3& b, const Vec3& c);
}

#pragma once

#include "Math/Matrix.h"
#include "Math/Vector.h"

#include <algorithm>
#include <limits>

namespace arc
{
    /// Axis-aligned bounding box. A default-constructed box is empty (min above max), so expanding
    /// it by its first point makes it exactly that point.
    struct Aabb
    {
        Vec3 min{std::numeric_limits<float>::max()};
        Vec3 max{std::numeric_limits<float>::lowest()};

        [[nodiscard]] bool isEmpty() const { return min.x > max.x || min.y > max.y || min.z > max.z; }

        void expand(const Vec3& point)
        {
            min = {std::min(min.x, point.x), std::min(min.y, point.y), std::min(min.z, point.z)};
            max = {std::max(max.x, point.x), std::max(max.y, point.y), std::max(max.z, point.z)};
        }

        void expand(const Aabb& other)
        {
            if (!other.isEmpty())
            {
                expand(other.min);
                expand(other.max);
            }
        }

        /// Only meaningful when not empty.
        [[nodiscard]] Vec3 center() const { return (min + max) * 0.5f; }
        [[nodiscard]] Vec3 size() const { return max - min; }

        /// The box around this box after `transform`: all eight corners transformed and enclosed.
        /// Larger than the transformed shape when it rotates, but never smaller.
        [[nodiscard]] Aabb transformed(const Mat4& transform) const
        {
            Aabb result;
            if (isEmpty())
                return result;
            for (int corner = 0; corner < 8; ++corner)
            {
                const Vec3 point{(corner & 1) ? max.x : min.x, (corner & 2) ? max.y : min.y, (corner & 4) ? max.z : min.z};
                result.expand(transform.transformPoint(point));
            }
            return result;
        }
    };
}

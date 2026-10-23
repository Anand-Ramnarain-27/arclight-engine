#include "Math/Frustum.h"

#include <cmath>
#include <limits>

namespace arc
{
    Frustum Frustum::fromCamera(const Vec3& position, const Vec3& forward, const Vec3& right, const Vec3& up,
                                float verticalFov, float aspect, float nearZ, float farZ)
    {
        // Each side plane passes through the camera and contains the forward axis tilted by half the
        // field of view toward that side; its normal is perpendicular to that edge and to the axis
        // the plane pivots on. The cross product's sign depends on the order, so it is fixed by
        // requiring the normal to lean forward, into the frustum.
        const float halfHeight = std::tan(verticalFov * 0.5f);
        const float halfWidth = halfHeight * aspect;
        const auto inward = [&forward](const Vec3& normal) { return dot(normal, forward) < 0.0f ? -normal : normal; };

        Frustum frustum;
        frustum.planes[0] = Plane::fromPointNormal(position, inward(cross(forward - right * halfWidth, up)));
        frustum.planes[1] = Plane::fromPointNormal(position, inward(cross(forward + right * halfWidth, up)));
        frustum.planes[2] = Plane::fromPointNormal(position, inward(cross(forward - up * halfHeight, right)));
        frustum.planes[3] = Plane::fromPointNormal(position, inward(cross(forward + up * halfHeight, right)));
        frustum.planes[4] = Plane::fromPointNormal(position + forward * nearZ, forward);
        frustum.planeCount = 5;
        if (std::isfinite(farZ))
            frustum.planes[frustum.planeCount++] = Plane::fromPointNormal(position + forward * farZ, -forward);
        return frustum;
    }

    namespace
    {
        // A box against the planes, given its centre and its "radius" along each plane's normal:
        // the box is entirely on a plane's negative side if its centre is further behind it than
        // that radius, and entirely in front if further in front.
        template <typename Radius>
        Containment classifyBox(const Frustum& frustum, const Vec3& center, Radius radius)
        {
            Containment result = Containment::Inside;
            for (uint32_t i = 0; i < frustum.planeCount; ++i)
            {
                const Plane& plane = frustum.planes[i];
                const float distance = plane.signedDistance(center);
                const float r = radius(plane.normal);
                if (distance < -r)
                    return Containment::Outside;
                if (distance < r)
                    result = Containment::Intersects;
            }
            return result;
        }
    }

    Containment Frustum::classify(const Aabb& box) const
    {
        if (box.isEmpty())
            return Containment::Outside;
        const Vec3 half = box.size() * 0.5f;
        return classifyBox(*this, box.center(), [&half](const Vec3& n) {
            return half.x * std::abs(n.x) + half.y * std::abs(n.y) + half.z * std::abs(n.z);
        });
    }

    Containment Frustum::classify(const Obb& box) const
    {
        return classifyBox(*this, box.center, [&box](const Vec3& n) {
            return std::abs(dot(n, box.halfAxes[0])) + std::abs(dot(n, box.halfAxes[1])) + std::abs(dot(n, box.halfAxes[2]));
        });
    }
}

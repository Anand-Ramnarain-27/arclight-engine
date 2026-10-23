#include "Math/Ray.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace arc
{
    std::optional<float> intersectRayAabb(const Ray& ray, const Aabb& box)
    {
        if (box.isEmpty())
            return std::nullopt;

        float tEnter = 0.0f;
        float tExit = std::numeric_limits<float>::infinity();
        const float origin[3] = {ray.origin.x, ray.origin.y, ray.origin.z};
        const float direction[3] = {ray.direction.x, ray.direction.y, ray.direction.z};
        const float min[3] = {box.min.x, box.min.y, box.min.z};
        const float max[3] = {box.max.x, box.max.y, box.max.z};
        for (int axis = 0; axis < 3; ++axis)
        {
            // A zero direction component gives +-infinity here, so a ray parallel to the slab is
            // kept if its origin is between the planes and rejected otherwise. Only an origin
            // exactly on a plane gives 0 * inf = NaN, which the comparisons below then ignore.
            const float inverse = 1.0f / direction[axis];
            float t0 = (min[axis] - origin[axis]) * inverse;
            float t1 = (max[axis] - origin[axis]) * inverse;
            if (t0 > t1)
                std::swap(t0, t1);
            tEnter = t0 > tEnter ? t0 : tEnter;
            tExit = t1 < tExit ? t1 : tExit;
            if (tEnter > tExit)
                return std::nullopt;
        }
        return tEnter;
    }

    std::optional<float> intersectRayTriangle(const Ray& ray, const Vec3& a, const Vec3& b, const Vec3& c)
    {
        // Solves origin + t * direction = a + u * (b - a) + v * (c - a) with Cramer's rule.
        const Vec3 edge1 = b - a;
        const Vec3 edge2 = c - a;
        const Vec3 p = cross(ray.direction, edge2);
        const float determinant = dot(edge1, p);
        // Relative to the edge lengths, so tiny and huge meshes are judged alike.
        const float scale = lengthSquared(edge1) * lengthSquared(edge2) * lengthSquared(ray.direction);
        if (determinant * determinant <= 1e-14f * scale)
            return std::nullopt;

        const float inverse = 1.0f / determinant;
        const Vec3 s = ray.origin - a;
        const float u = dot(s, p) * inverse;
        if (u < 0.0f || u > 1.0f)
            return std::nullopt;
        const Vec3 q = cross(s, edge1);
        const float v = dot(ray.direction, q) * inverse;
        if (v < 0.0f || u + v > 1.0f)
            return std::nullopt;
        const float t = dot(edge2, q) * inverse;
        if (t < 0.0f)
            return std::nullopt;
        return t;
    }
}

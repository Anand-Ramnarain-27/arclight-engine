#pragma once

#include "Math/Ray.h"

#include <cstdint>
#include <optional>

namespace arc
{
    class GameObject;
    class MeshComponent;
    class Scene;

    struct RaycastHit
    {
        GameObject* object = nullptr;
        const MeshComponent* mesh = nullptr;
        /// Along the ray, in multiples of its direction (world units for a unit direction).
        float distance = 0.0f;
        Vec3 point;               // World space.
        uint32_t triangle = 0;    // Index of the triangle within the mesh's model, for debugging.
    };

    /// The nearest triangle of an enabled mesh of an active object that the world-space ray hits,
    /// from either side. Each mesh is first tested against its bounds, then each primitive against
    /// its own, then triangle by triangle. Ignores alpha: a masked-out pixel still counts.
    [[nodiscard]] std::optional<RaycastHit> raycast(const Scene& scene, const Ray& ray);
}

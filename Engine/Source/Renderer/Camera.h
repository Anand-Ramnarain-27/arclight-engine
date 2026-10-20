#pragma once

#include "Math/Math.h"
#include "Math/Ray.h"
#include "RHI/RHITypes.h"

namespace arc
{
    /// A perspective camera: where it is, where it looks, and how it projects.
    ///
    /// Orientation is yaw and pitch rather than a free quaternion, so the horizon always stays
    /// level, which is what an editor or first-person camera wants. Yaw 0 and pitch 0 look down -Z.
    ///
    /// Depth is reversed by default (near plane at depth 1, infinity at 0); see reversedZ.
    struct Camera
    {
        Vec3 position{0.0f, 2.0f, 6.0f};
        float yaw = 0.0f;                         // Radians around +Y; positive turns left.
        float pitch = 0.0f;                       // Radians around the camera's X; positive looks up.
        float verticalFov = toRadians(60.0f);     // Radians.
        float nearZ = 0.1f;                       // World units; must be above 0.
        /// World units. Only used without reversed-Z: the reversed projection puts the far plane at
        /// infinity.
        float farZ = 1000.0f;

        /// Floats have the most precision near 0. A standard projection maps the far distance, where
        /// precision is needed most, close to depth 1, and a reversed one close to 0, which spreads
        /// the precision almost evenly with distance. Needs a GreaterEqual depth test and clearing
        /// depth to 0 instead of LessEqual and 1; depthCompare() and clearDepth() give the right pair.
        bool reversedZ = true;

        /// Pitch is kept this far inside straight up or down, where yaw would stop meaning anything.
        static constexpr float kMaxPitch = toRadians(89.0f);

        [[nodiscard]] Quat rotation() const { return Quat::fromEuler({pitch, yaw, 0.0f}); }
        [[nodiscard]] Vec3 forward() const { return rotation().rotate(Vec3::forward()); }
        [[nodiscard]] Vec3 right() const { return rotation().rotate(Vec3::right()); }
        [[nodiscard]] Vec3 up() const { return rotation().rotate(Vec3::up()); }

        /// Points the camera along `direction`, which need not be normalised.
        void lookAlong(const Vec3& direction);

        /// World space to view space: the inverse of the camera's own transform.
        [[nodiscard]] Mat4 view() const;

        /// View space to clip space. `aspect` is width / height.
        [[nodiscard]] Mat4 projection(float aspect) const;

        /// World space: from the camera's position through a point of the image. `ndc` is that point
        /// in normalised device coordinates, [-1, 1] across the view with +Y up; `aspect` is width /
        /// height. The direction has unit length, so t is a distance.
        [[nodiscard]] Ray viewportRay(Vec2 ndc, float aspect) const;

        [[nodiscard]] float clearDepth() const { return reversedZ ? 0.0f : 1.0f; }

        /// Equal passes too, so a second pass over the same geometry (a later outline or decal)
        /// still draws.
        [[nodiscard]] rhi::CompareOp depthCompare() const
        {
            return reversedZ ? rhi::CompareOp::GreaterEqual : rhi::CompareOp::LessEqual;
        }
    };
}

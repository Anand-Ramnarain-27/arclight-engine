#pragma once

#include "Math/Vector.h"

#include <cstdint>

namespace arc
{
    // The cube texture conventions of D3D12 and Metal (the same as OpenGL's and Vulkan's): faces
    // in the order +X, -X, +Y, -Y, +Z, -Z, each image's first row at the top, and a cube space that
    // is left-handed. The engine's world is right-handed with -Z forward, so a world direction
    // samples the cube at (x, y, -z): looking forward sees the +Z face, as a skybox authored for a
    // left-handed engine expects.

    enum class CubeFace : uint32_t
    {
        PositiveX,
        NegativeX,
        PositiveY,
        NegativeY,
        PositiveZ,
        NegativeZ,
    };

    /// The cube-space direction through a point of a face. `s` and `t` are in [-1, 1], left to right
    /// and top to bottom across the face's image. Not normalised.
    [[nodiscard]] Vec3 cubeFaceDirection(CubeFace face, float s, float t);

    struct CubeFacePoint
    {
        CubeFace face = CubeFace::PositiveX;
        float s = 0.0f;   // [-1, 1], left to right.
        float t = 0.0f;   // [-1, 1], top to bottom.
    };

    /// The face and point a cube-space direction samples: the face of its largest component.
    [[nodiscard]] CubeFacePoint cubeFacePoint(const Vec3& direction);

    /// A world direction in cube space, and back (the conversion is its own inverse).
    [[nodiscard]] constexpr Vec3 worldToCube(const Vec3& world) { return {world.x, world.y, -world.z}; }
}

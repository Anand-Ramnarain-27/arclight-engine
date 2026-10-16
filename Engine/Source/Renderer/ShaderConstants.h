#pragma once

#include "Math/Math.h"

#include <cstddef>

namespace arc
{
    // C++ mirrors of the cbuffers in Engine/Shaders/Constants.hlsli, uploaded with
    // CommandList::setConstants().
    //
    // HLSL packs a cbuffer into 16-byte rows, and a member never straddles a row: a float3
    // followed by a float shares one row, but a float3 followed by a float2 pushes the float2 to
    // the next row. C++ has no such rule, so each mirror spells out its padding and checks every
    // offset with static_assert. Matrices need no transpose: both sides are column-major.

    struct ViewConstants
    {
        Mat4 viewProjection;          // World space to clip space.
        Mat4 inverseViewProjection;   // Clip space back to world space, for reconstructing rays.
        Vec3 cameraPosition;          // World space.
        float padding = 0.0f;
    };
    static_assert(offsetof(ViewConstants, viewProjection) == 0);
    static_assert(offsetof(ViewConstants, inverseViewProjection) == 64);
    static_assert(offsetof(ViewConstants, cameraPosition) == 128);
    static_assert(sizeof(ViewConstants) == 144);
}

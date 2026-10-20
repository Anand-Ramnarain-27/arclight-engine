// The silhouettes of the selected meshes, written as 1 into the selection mask that Outline.hlsl
// reads. No depth test: the outline follows the whole silhouette, also where something covers it.

#include "Constants.hlsli"

// Mirrored by MaskDrawConstants in Editor/Source/Scene/SelectionOutline.cpp.
cbuffer MaskDrawConstants : register(b1)
{
    float4x4 world;   // Object space to world space.
};

struct VSInput
{
    float3 position : POSITION;
};

float4 VSMain(VSInput input) : SV_Position
{
    return mul(viewProjection, mul(world, float4(input.position, 1.0)));
}

float4 PSMain() : SV_Target0
{
    return float4(1.0, 1.0, 1.0, 1.0);
}

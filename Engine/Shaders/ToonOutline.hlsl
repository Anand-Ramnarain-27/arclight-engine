// Inverted-hull outlines for toon materials: the mesh drawn a second time with its front faces
// culled and every vertex pushed out along its normal. Only the back faces of the grown shell are
// left, and the mesh itself hides them except at the silhouette and wherever the surface folds
// away from the camera, so a band of the outline colour shows there.
//
// The push happens in clip space, along the normal's direction on screen, scaled by w: the band is
// then the same number of pixels wide at any distance. Vertices on a hard edge carry one normal per
// face, which pulls the shell apart into gaps at that edge; meshes with smooth normals give a closed
// outline.

#include "Constants.hlsli"

// Same layout as DrawConstants in Mesh.hlsl, so one upload serves both passes.
cbuffer DrawConstants : register(b1)
{
    float4x4 world;
    float4x4 normalMatrix;
    uint debugView;
    float handedness;
};

// Mirrored by OutlineConstants in Engine/Source/Renderer/MeshRenderer.cpp.
cbuffer OutlineConstants : register(b2)
{
    float3 outlineColor;    // Linear RGB.
    float outlineWidth;     // Pixels.
    float2 viewportSize;    // Pixels.
};

struct VSInput
{
    float3 position : POSITION;
    float3 normal : NORMAL;
    float2 uv : TEXCOORD0;
    float4 tangent : TANGENT;
};

float4 VSMain(VSInput input) : SV_Position
{
    const float4 position = mul(viewProjection, mul(world, float4(input.position, 1.0)));
    const float3 worldNormal = mul((float3x3)normalMatrix, input.normal);
    const float2 screenNormal = mul(viewProjection, float4(worldNormal, 0.0)).xy;
    const float lengthSquared = dot(screenNormal, screenNormal);
    if (lengthSquared < 1e-12)
        return position;
    // NDC spans 2 units across the viewport; multiplying by w undoes the perspective divide.
    const float2 offset = screenNormal * rsqrt(lengthSquared) * outlineWidth * 2.0 / viewportSize;
    return float4(position.xy + offset * position.w, position.zw);
}

float4 PSMain() : SV_Target0
{
    return float4(outlineColor, 1.0);
}

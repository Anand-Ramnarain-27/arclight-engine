// Meshes imported from glTF: a base colour (factor times texture) under placeholder lighting, a
// fixed directional light and a sky/ground ambient, until physically based lighting is added.

#include "Common.hlsli"
#include "Constants.hlsli"

// Mirrored by MeshDrawConstants in Engine/Source/Renderer/MeshRenderer.cpp.
cbuffer DrawConstants : register(b1)
{
    float4x4 world;          // Object space to world space.
    float4x4 normalMatrix;   // Inverse transpose of world's upper 3x3, for normals.
    uint debugView;          // One of the DEBUG_VIEW_ values below.
};

// Mirrored by MaterialConstants in Engine/Source/Renderer/MeshRenderer.cpp.
cbuffer MaterialConstants : register(b2)
{
    float4 baseColorFactor;  // Linear RGB and alpha, multiplied with the texture.
    float alphaCutoff;
    uint alphaMode;          // One of the ALPHA_MODE_ values below.
};

// Match arc::AlphaMode and MeshRenderer::DebugView.
#define ALPHA_MODE_OPAQUE 0
#define ALPHA_MODE_MASK 1
#define ALPHA_MODE_BLEND 2

#define DEBUG_VIEW_LIT 0
#define DEBUG_VIEW_BASE_COLOR 1
#define DEBUG_VIEW_NORMALS 2
#define DEBUG_VIEW_TEXCOORDS 3

Texture2D<float4> g_baseColor : register(t0);
SamplerState g_sampler : register(s0);

struct VSInput
{
    float3 position : POSITION;
    float3 normal : NORMAL;
    float2 uv : TEXCOORD0;
};

struct VSOutput
{
    float4 position : SV_Position;
    float3 normal : NORMAL;   // World space, not normalised: interpolation shortens it anyway.
    float2 uv : TEXCOORD0;
};

VSOutput VSMain(VSInput input)
{
    VSOutput output;
    output.position = mul(viewProjection, mul(world, float4(input.position, 1.0)));
    output.normal = mul((float3x3)normalMatrix, input.normal);
    output.uv = input.uv;
    return output;
}

float4 PSMain(VSOutput input, bool isFrontFace : SV_IsFrontFace) : SV_Target0
{
    // The texture is sRGB, so sampling returns linear colour, which the factor multiplies.
    float4 baseColor = g_baseColor.Sample(g_sampler, input.uv) * baseColorFactor;
    if (alphaMode == ALPHA_MODE_MASK)
        clip(baseColor.a - alphaCutoff);
    const float alpha = alphaMode == ALPHA_MODE_BLEND ? baseColor.a : 1.0;

    // Double-sided materials show their back faces; those face away from their normal.
    float3 normal = normalize(input.normal);
    if (!isFrontFace)
        normal = -normal;

    if (debugView == DEBUG_VIEW_NORMALS)
        return float4(normal * 0.5 + 0.5, 1.0);
    if (debugView == DEBUG_VIEW_TEXCOORDS)
        return float4(frac(input.uv), 0.0, 1.0);
    if (debugView == DEBUG_VIEW_BASE_COLOR)
        return float4(LinearToSrgb(baseColor.rgb), alpha);

    const float3 lightDirection = normalize(float3(0.4, 0.8, 0.45));   // Toward the light.
    const float3 skyColor = float3(0.30, 0.34, 0.42);
    const float3 groundColor = float3(0.12, 0.10, 0.09);
    const float3 ambient = lerp(groundColor, skyColor, normal.y * 0.5 + 0.5);
    const float diffuse = saturate(dot(normal, lightDirection));
    const float3 color = baseColor.rgb * (ambient + diffuse * float3(1.0, 0.96, 0.9));
    return float4(LinearToSrgb(color), alpha);
}

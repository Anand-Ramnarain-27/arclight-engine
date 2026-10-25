// Meshes imported from glTF, lit by the scene's lights (Lighting.hlsli) into the HDR scene target.
// The material is glTF's metallic-roughness model: base colour, metallic and roughness, each a factor
// times a texture.
//
// Material textures have fixed slots (MaterialTextureSlot in MeshRenderer.h), filled with a white
// stand-in when a material has no such texture, so the factor alone decides. They share the base
// colour texture's sampler in s0: glTF lets each texture name its own, but files almost never use
// different ones within a material.

#include "Common.hlsli"
#include "Constants.hlsli"
#include "Lighting.hlsli"

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
    float roughnessFactor;   // Perceptual roughness, 0 to 1.
    float metallicFactor;    // 0 for a dielectric, 1 for a metal.
};

// Match arc::AlphaMode and MeshRenderer::DebugView.
#define ALPHA_MODE_OPAQUE 0
#define ALPHA_MODE_MASK 1
#define ALPHA_MODE_BLEND 2

#define DEBUG_VIEW_LIT 0
#define DEBUG_VIEW_BASE_COLOR 1
#define DEBUG_VIEW_NORMALS 2
#define DEBUG_VIEW_TEXCOORDS 3
#define DEBUG_VIEW_DIFFUSE 4
#define DEBUG_VIEW_SPECULAR 5
#define DEBUG_VIEW_LIGHTING 6
#define DEBUG_VIEW_METALLIC 7
#define DEBUG_VIEW_ROUGHNESS 8

// Specular reflectance at normal incidence of common dielectrics (index of refraction about 1.5).
static const float kDielectricF0 = 0.04;

Texture2D<float4> g_baseColor : register(t0);           // sRGB colour, alpha.
Texture2D<float4> g_metallicRoughness : register(t1);   // Linear: green = roughness, blue = metallic.
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
    float3 worldPosition : POSITION;
    float3 normal : NORMAL;   // World space, not normalised: interpolation shortens it anyway.
    float2 uv : TEXCOORD0;
};

VSOutput VSMain(VSInput input)
{
    VSOutput output;
    const float4 worldPosition = mul(world, float4(input.position, 1.0));
    output.position = mul(viewProjection, worldPosition);
    output.worldPosition = worldPosition.xyz;
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

    // Data views are shown untouched by exposure and the tone curve (ToneMapper::passThrough), which
    // still sRGB-encodes, so values meant to be seen as they are are decoded here first.
    if (debugView == DEBUG_VIEW_NORMALS)
        return float4(SrgbToLinear(normal * 0.5 + 0.5), 1.0);
    if (debugView == DEBUG_VIEW_TEXCOORDS)
        return float4(SrgbToLinear(float3(frac(input.uv), 0.0)), 1.0);
    if (debugView == DEBUG_VIEW_BASE_COLOR)
        return float4(baseColor.rgb, alpha);

    const float4 metallicRoughness = g_metallicRoughness.Sample(g_sampler, input.uv);
    float metallic = saturate(metallicFactor * metallicRoughness.b);
    float roughness = saturate(roughnessFactor * metallicRoughness.g);
    if (metallicOverride >= 0.0)
        metallic = metallicOverride;
    if (roughnessOverride >= 0.0)
        roughness = roughnessOverride;
    if (debugView == DEBUG_VIEW_METALLIC)
        return float4(SrgbToLinear(metallic.xxx), 1.0);
    if (debugView == DEBUG_VIEW_ROUGHNESS)
        return float4(SrgbToLinear(roughness.xxx), 1.0);

    // A metal reflects its base colour and absorbs what it refracts, so it has no diffuse part; a
    // dielectric reflects about 4% whatever its colour, and its base colour is the diffuse albedo.
    const float3 albedo = debugView == DEBUG_VIEW_LIGHTING ? 1.0 : baseColor.rgb;
    Surface surface;
    surface.position = input.worldPosition;
    surface.normal = normal;
    surface.view = normalize(cameraPosition - input.worldPosition);
    surface.diffuseColor = albedo * (1.0 - metallic);
    surface.f0 = lerp(kDielectricF0.xxx, albedo, metallic);
    surface.roughness = lightingModel == LIGHTING_MODEL_COOK_TORRANCE ? max(roughness, kMinRoughness) : roughness;

    float3 diffuse;
    float3 specular;
    ShadeSurface(surface, diffuse, specular);

    if (debugView == DEBUG_VIEW_DIFFUSE)
        return float4(diffuse, alpha);
    if (debugView == DEBUG_VIEW_SPECULAR)
        return float4(specular, alpha);
    return float4(diffuse + specular, alpha);
}

// Meshes imported from glTF, lit by the scene's lights (Lighting.hlsli) into the HDR scene target.
// The material is glTF's metallic-roughness model: base colour, metallic, roughness, a tangent-space
// normal map, ambient occlusion and emission, each a factor times a texture. An object can shade a
// material as toon instead (Toon.hlsli), with the same textures.
//
// Material textures have fixed slots (MaterialTextureSlot in MeshRenderer.h), filled with a stand-in
// when a material has no such texture: white, so the factor alone decides, or a flat normal. They
// share the base colour texture's sampler in s0: glTF lets each texture name its own, but files
// almost never use different ones within a material.

#include "Common.hlsli"
#include "Constants.hlsli"
#include "Lighting.hlsli"
#include "Toon.hlsli"

// Mirrored by MeshDrawConstants in Engine/Source/Renderer/MeshRenderer.cpp.
cbuffer DrawConstants : register(b1)
{
    float4x4 world;          // Object space to world space.
    float4x4 normalMatrix;   // Inverse transpose of world's upper 3x3, for normals.
    uint debugView;          // One of the DEBUG_VIEW_ values below.
    float handedness;        // -1 when world mirrors (negative determinant), else 1.
};

// Mirrored by MaterialConstants in Engine/Source/Renderer/MeshRenderer.cpp.
cbuffer MaterialConstants : register(b2)
{
    float4 baseColorFactor;    // Linear RGB and alpha, multiplied with the texture.
    float alphaCutoff;
    uint alphaMode;            // One of the ALPHA_MODE_ values below.
    float roughnessFactor;     // Perceptual roughness, 0 to 1.
    float metallicFactor;      // 0 for a dielectric, 1 for a metal.
    float3 emissiveFactor;     // Linear RGB, any brightness.
    float normalScale;         // Multiplies the normal map's x and y.
    float occlusionStrength;   // 0 ignores the occlusion texture, 1 applies it fully.
    uint shadingModel;         // One of the SHADING_ values below.
    uint toonBands;
    float toonSoftness;
    float toonSpecular;
    float toonRimSize;
    float toonRim;
    uint toonFlags;            // TOON_FLAG_ bits.
};

// Match arc::AlphaMode, arc::ShadingModel and MeshRenderer::DebugView.
#define ALPHA_MODE_OPAQUE 0
#define ALPHA_MODE_MASK 1
#define ALPHA_MODE_BLEND 2

#define SHADING_PHYSICAL 0
#define SHADING_TOON 1

#define DEBUG_VIEW_LIT 0
#define DEBUG_VIEW_BASE_COLOR 1
#define DEBUG_VIEW_NORMALS 2
#define DEBUG_VIEW_TEXCOORDS 3
#define DEBUG_VIEW_DIFFUSE 4
#define DEBUG_VIEW_SPECULAR 5
#define DEBUG_VIEW_LIGHTING 6
#define DEBUG_VIEW_METALLIC 7
#define DEBUG_VIEW_ROUGHNESS 8
#define DEBUG_VIEW_OCCLUSION 9
#define DEBUG_VIEW_EMISSIVE 10
#define DEBUG_VIEW_TANGENTS 11

// Specular reflectance at normal incidence of common dielectrics (index of refraction about 1.5).
static const float kDielectricF0 = 0.04;

Texture2D<float4> g_baseColor : register(t0);           // sRGB colour, alpha.
Texture2D<float4> g_metallicRoughness : register(t1);   // Linear: green = roughness, blue = metallic.
Texture2D<float4> g_normal : register(t2);              // Linear: tangent-space normal's x, y * 0.5 + 0.5.
Texture2D<float4> g_occlusion : register(t3);           // Linear: red = ambient light reaching the surface.
Texture2D<float4> g_emissive : register(t4);            // sRGB colour.
SamplerState g_sampler : register(s0);

struct VSInput
{
    float3 position : POSITION;
    float3 normal : NORMAL;
    float2 uv : TEXCOORD0;
    float4 tangent : TANGENT;
};

struct VSOutput
{
    float4 position : SV_Position;
    float3 worldPosition : POSITION;
    float3 normal : NORMAL;     // World space, not normalised: interpolation shortens it anyway.
    float4 tangent : TANGENT;   // World space; w is the bitangent's sign, mirroring included.
    float2 uv : TEXCOORD0;
};

VSOutput VSMain(VSInput input)
{
    VSOutput output;
    const float4 worldPosition = mul(world, float4(input.position, 1.0));
    output.position = mul(viewProjection, worldPosition);
    output.worldPosition = worldPosition.xyz;
    output.normal = mul((float3x3)normalMatrix, input.normal);
    // Tangents lie in the surface, so they transform like positions, by world itself. A mirroring
    // transform turns the frame left-handed, which flips the bitangent cross(normal, tangent) builds.
    output.tangent = float4(mul((float3x3)world, input.tangent.xyz), input.tangent.w * handedness);
    output.uv = input.uv;
    return output;
}

// The shading normal: the normal map's direction in the frame of the surface's normal, tangent and
// bitangent.
float3 MappedNormal(float3 normal, float3 tangent, float3 bitangent, float2 uv)
{
    // Only x and y are read: z of a unit normal facing out of the surface follows from them, which
    // lets cooked normal maps be two-channel BC5 (whose blue reads 0).
    float3 tangentSpace;
    tangentSpace.xy = g_normal.Sample(g_sampler, uv).xy * 2.0 - 1.0;
    tangentSpace.z = sqrt(saturate(1.0 - dot(tangentSpace.xy, tangentSpace.xy)));
    tangentSpace.xy *= normalScale;
    return normalize(tangent * tangentSpace.x + bitangent * tangentSpace.y + normal * tangentSpace.z);
}

float4 PSMain(VSOutput input, bool isFrontFace : SV_IsFrontFace) : SV_Target0
{
    // The texture is sRGB, so sampling returns linear colour, which the factor multiplies.
    float4 baseColor = g_baseColor.Sample(g_sampler, input.uv) * baseColorFactor;
    if (alphaMode == ALPHA_MODE_MASK)
        clip(baseColor.a - alphaCutoff);
    const float alpha = alphaMode == ALPHA_MODE_BLEND ? baseColor.a : 1.0;

    // Interpolation leaves the tangent no longer perpendicular to the normal, so it is made so again
    // (Gram-Schmidt) before the bitangent is derived from the two.
    float3 normal = normalize(input.normal);
    float3 tangent = input.tangent.xyz - normal * dot(normal, input.tangent.xyz);
    const float tangentLength = length(tangent);
    tangent = tangentLength > 1e-6 ? tangent / tangentLength : float3(0.0, 0.0, 0.0);
    float3 bitangent = cross(normal, tangent) * input.tangent.w;
    // Double-sided materials show their back faces, which face away from their normal. The whole
    // frame turns over with it, so the normal map still bends light the same way on both sides.
    if (!isFrontFace)
    {
        normal = -normal;
        tangent = -tangent;
        bitangent = -bitangent;
    }
    if ((lightingFlags & LIGHTING_FLAG_NORMAL_MAPS) != 0 && tangentLength > 1e-6)
        normal = MappedNormal(normal, tangent, bitangent, input.uv);

    // Data views are shown untouched by exposure and the tone curve (ToneMapper::passThrough), which
    // still sRGB-encodes, so values meant to be seen as they are are decoded here first.
    if (debugView == DEBUG_VIEW_NORMALS)
        return float4(SrgbToLinear(normal * 0.5 + 0.5), 1.0);
    if (debugView == DEBUG_VIEW_TANGENTS)
        return float4(SrgbToLinear(tangent * 0.5 + 0.5), 1.0);
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

    // glTF: occlusion = 1 + strength * (texture - 1), so strength 0 leaves the surface unoccluded.
    float occlusion = 1.0 + occlusionStrength * (g_occlusion.Sample(g_sampler, input.uv).r - 1.0);
    if ((lightingFlags & LIGHTING_FLAG_OCCLUSION) == 0)
        occlusion = 1.0;
    if (debugView == DEBUG_VIEW_OCCLUSION)
        return float4(SrgbToLinear(occlusion.xxx), 1.0);

    const float3 emissive = (lightingFlags & LIGHTING_FLAG_EMISSIVE) != 0
                                ? emissiveFactor * g_emissive.Sample(g_sampler, input.uv).rgb
                                : float3(0.0, 0.0, 0.0);
    if (debugView == DEBUG_VIEW_EMISSIVE)
        return float4(SceneRadiance(emissive), alpha);

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
    surface.occlusion = occlusion;

    float3 diffuse;
    float3 specular;
    if (shadingModel == SHADING_TOON)
    {
        // A metal's colour goes into its highlight, but a toon surface keeps its base colour in the
        // bands too: a black metal reads as a hole in a flat-shaded image.
        ToonParams params;
        params.bands = max(toonBands, 1u);
        params.softness = toonSoftness;
        params.specular = toonSpecular;
        params.rimSize = toonRimSize;
        params.rim = toonRim;
        params.ramp = (toonFlags & TOON_FLAG_RAMP) != 0;
        surface.diffuseColor = albedo;
        ShadeToon(surface, params, lerp(1.0.xxx, albedo, metallic), diffuse, specular);
    }
    else
    {
        ShadeSurface(surface, diffuse, specular);
    }

    if (debugView == DEBUG_VIEW_DIFFUSE)
        return float4(SceneRadiance(diffuse), alpha);
    if (debugView == DEBUG_VIEW_SPECULAR)
        return float4(SceneRadiance(specular), alpha);
    if (debugView == DEBUG_VIEW_LIGHTING)
        return float4(SceneRadiance(diffuse + specular), alpha);
    return float4(SceneRadiance(diffuse + specular + emissive), alpha);
}

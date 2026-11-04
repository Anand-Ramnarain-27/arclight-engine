// Lights and the reflectance models that shade them. Units follow glTF's KHR_lights_punctual:
// directional lights give illuminance in lux, point and spot lights intensity in candela, so the
// light arriving at a surface is radiance * attenuation, and what leaves toward the eye is
// BRDF * incoming * cos(angle to the normal).
//
// Mirrored by Engine/Source/Renderer/Lighting.h; change both together.

#ifndef ARCLIGHT_LIGHTING_HLSLI
#define ARCLIGHT_LIGHTING_HLSLI

#include "Brdf.hlsli"

#define LIGHT_DIRECTIONAL 0
#define LIGHT_POINT 1
#define LIGHT_SPOT 2

#define LIGHTING_MODEL_LAMBERT 0
#define LIGHTING_MODEL_PHONG 1
#define LIGHTING_MODEL_BLINN_PHONG 2
#define LIGHTING_MODEL_COOK_TORRANCE 3

#define LIGHTING_FLAG_ENERGY_CONSERVING 1
#define LIGHTING_FLAG_FRESNEL 2
#define LIGHTING_FLAG_NORMAL_MAPS 4
#define LIGHTING_FLAG_OCCLUSION 8
#define LIGHTING_FLAG_EMISSIVE 16
#define LIGHTING_FLAG_ENVIRONMENT 32

// Mirrored by GpuLight.
struct Light
{
    float3 position;    // World space.
    float range;        // Metres at which the light has faded to zero; 0 for no limit.
    float3 direction;   // World space, unit length: the way the light travels.
    uint type;          // LIGHT_ value.
    float3 radiance;    // Linear colour times intensity.
    float spotScale;
    float spotOffset;
    float3 padding;
};

// Mirrored by LightingConstants. Set once a frame.
cbuffer LightingConstants : register(b3)
{
    float3 ambientSky;      // Ambient light from straight above, times the ambient intensity.
    uint lightCount;
    float3 ambientGround;   // From straight below.
    uint lightingModel;     // LIGHTING_MODEL_ value.
    uint lightingFlags;     // LIGHTING_FLAG_ bits.
    float roughnessOverride;   // Replaces every material's roughness when 0 or more.
    float metallicOverride;    // Replaces every material's metallic when 0 or more.
    uint lightingPadding;
    float environmentIntensity;   // Multiplies the environment's light.
    float prefilteredMaxLod;      // The prefiltered cube's last mip level, which holds roughness 1.
    float2 environmentRotation;   // cos and sin of the environment's turn about +Y.
};

// t8 leaves t0..t7 for material and environment textures (structured buffers cannot use t0..t3,
// whose Metal buffer indices belong to b0..b3).
StructuredBuffer<Light> g_lights : register(t8);

// Image-based lighting, made from the environment by EnvironmentMaps (Ibl*.hlsl): the light
// arriving from every direction, convolved for diffuse and for each roughness, and the split-sum
// BRDF table.
TextureCube<float4> g_irradiance : register(t5);    // Irradiance / pi, by normal.
TextureCube<float4> g_prefiltered : register(t6);   // Radiance convolved with GGX, mip = roughness.
Texture2D<float4> g_brdfLut : register(t7);         // (A, B) by (cos(n, v), roughness).
SamplerState g_environmentSampler : register(s1);   // Trilinear, clamped.

// A world direction as the environment cubes are sampled: turned by the environment's rotation,
// then into cube space (see Engine/Source/Renderer/CubeMap.h).
float3 EnvironmentDirection(float3 world)
{
    const float c = environmentRotation.x;
    const float s = environmentRotation.y;
    const float3 turned = float3(c * world.x - s * world.z, world.y, s * world.x + c * world.z);
    return float3(turned.x, turned.y, -turned.z);
}

// A surface point, ready to be lit.
struct Surface
{
    float3 position;       // World space.
    float3 normal;         // World space, unit length, facing the viewer's side.
    float3 view;           // Unit vector from the surface toward the camera.
    float3 diffuseColor;   // Albedo of the diffuse part: base colour, zero for metals.
    float3 f0;             // Specular reflectance at normal incidence: 0.04 for dielectrics, base colour for metals.
    float roughness;       // Perceptual, 0 to 1, as in glTF.
    float occlusion;       // How much ambient light reaches the surface, 0 to 1.
};

// The fraction of a light's radiance that reaches `position`, and the direction toward it.
float LightAttenuation(Light light, float3 position, out float3 toLight)
{
    if (light.type == LIGHT_DIRECTIONAL)
    {
        toLight = -light.direction;
        return 1.0;
    }

    const float3 offset = light.position - position;
    const float distanceSquared = max(dot(offset, offset), 1e-8);
    toLight = offset * rsqrt(distanceSquared);

    // Inverse-square falloff, faded smoothly to zero at the range so the light can be skipped
    // beyond it (KHR_lights_punctual's recommended window). The 1 cm floor keeps the falloff finite
    // at the light itself.
    float attenuation = 1.0 / max(distanceSquared, 1e-4);
    if (light.range > 0.0)
    {
        const float ratio = distanceSquared / (light.range * light.range);
        const float window = saturate(1.0 - ratio * ratio);
        attenuation *= window * window;
    }

    if (light.type == LIGHT_SPOT)
    {
        const float cone = saturate(dot(light.direction, -toLight) * light.spotScale + light.spotOffset);
        attenuation *= cone * cone;
    }
    return attenuation;
}

// Light reflected toward the viewer from one light. The BRDF is Lambert's diffuse plus a specular
// lobe, both times the incoming light and the cosine of its angle to the normal.
//   Lambert        diffuse / pi. A perfectly matte surface sends the light it receives out equally
//                  in all directions; dividing by pi makes the total that leaves equal what came in.
//   Phong          cos(reflected light, view)^n. Normalised by (n + 2) / (2 pi).
//   Blinn-Phong    cos(normal, half vector)^n. Normalised by (n + 8) / (8 pi).
//   Cook-Torrance  D * G / (4 cos(n,l) cos(n,v)): microfacet theory, normalised by construction.
// Without the normalisation a sharper lobe (higher n) only gets smaller, so shiny surfaces reflect
// less light in total than rough ones; with it the peak rises as the lobe narrows and the energy
// stays the same. Fresnel scales the specular part, and whatever is reflected specularly is no
// longer available to the diffuse part.
void ShadeLight(Surface surface, float3 toLight, float3 incoming, out float3 diffuse, out float3 specular)
{
    diffuse = 0.0;
    specular = 0.0;
    const float nDotL = dot(surface.normal, toLight);
    if (nDotL <= 0.0)
        return;

    const float3 halfVector = normalize(toLight + surface.view);
    const bool fresnel = (lightingFlags & LIGHTING_FLAG_FRESNEL) != 0;
    const float3 reflectance = fresnel ? FresnelSchlick(surface.f0, dot(surface.view, halfVector)) : surface.f0;

    float lobe = 0.0;
    if (lightingModel == LIGHTING_MODEL_COOK_TORRANCE)
    {
        const float alpha = surface.roughness * surface.roughness;
        const float nDotV = max(dot(surface.normal, surface.view), 1e-4);
        const float nDotH = saturate(dot(surface.normal, halfVector));
        lobe = DistributionGgx(nDotH, alpha) * VisibilitySmithGgxCorrelated(nDotV, nDotL, alpha);
    }
    else if (lightingModel != LIGHTING_MODEL_LAMBERT)
    {
        const bool normalised = (lightingFlags & LIGHTING_FLAG_ENERGY_CONSERVING) != 0;
        const float blinnExponent = BlinnPhongExponent(surface.roughness);
        if (lightingModel == LIGHTING_MODEL_PHONG)
        {
            // A Phong lobe is about four times as wide as a Blinn-Phong lobe of the same exponent,
            // since the half vector moves half as far as the reflected vector.
            const float n = max(blinnExponent * 0.25, 1.0);
            const float3 reflected = reflect(-toLight, surface.normal);
            lobe = pow(saturate(dot(reflected, surface.view)), n);
            if (normalised)
                lobe *= (n + 2.0) / (2.0 * PI);
        }
        else
        {
            const float n = blinnExponent;
            lobe = pow(saturate(dot(surface.normal, halfVector)), n);
            if (normalised)
                lobe *= (n + 8.0) / (8.0 * PI);
        }
    }

    const float3 light = incoming * nDotL;
    specular = reflectance * lobe * light;
    diffuse = (fresnel ? 1.0 - reflectance : 1.0) * surface.diffuseColor / PI * light;
}

// Light arriving from everywhere around a normal, as irradiance / pi: what a white Lambertian surface
// facing that way reflects. The environment's irradiance cube, or the sky and ground hemisphere.
float3 AmbientIrradiance(float3 normal)
{
    if ((lightingFlags & LIGHTING_FLAG_ENVIRONMENT) != 0)
        return g_irradiance.SampleLevel(g_environmentSampler, EnvironmentDirection(normal), 0).rgb * environmentIntensity;
    return lerp(ambientGround, ambientSky, normal.y * 0.5 + 0.5);
}

// Every light in the list, plus the ambient light: the environment's image-based lighting, or the
// sky and ground hemisphere when that is off. Occlusion darkens the ambient light only. Direct light
// is not occluded: shadows (Phase 6) are what block it, and the occlusion texture is baked for light
// that arrives from everywhere.
void ShadeSurface(Surface surface, out float3 diffuse, out float3 specular)
{
    diffuse = 0.0;
    specular = 0.0;
    for (uint i = 0; i < lightCount; ++i)
    {
        const Light light = g_lights[i];
        float3 toLight;
        const float attenuation = LightAttenuation(light, surface.position, toLight);
        if (attenuation <= 0.0)
            continue;
        float3 lightDiffuse;
        float3 lightSpecular;
        ShadeLight(surface, toLight, light.radiance * attenuation, lightDiffuse, lightSpecular);
        diffuse += lightDiffuse;
        specular += lightSpecular;
    }

    if ((lightingFlags & LIGHTING_FLAG_ENVIRONMENT) != 0)
    {
        // The split-sum approximation (Karis 2013): the environment convolved per roughness, times
        // the BRDF's integral for this view angle and roughness, read from the table as f0 * A + B.
        const float nDotV = max(dot(surface.normal, surface.view), 1e-4);
        diffuse += AmbientIrradiance(surface.normal) * surface.diffuseColor * surface.occlusion;
        if (lightingModel == LIGHTING_MODEL_COOK_TORRANCE)
        {
            const float3 reflected = reflect(-surface.view, surface.normal);
            const float3 radiance = g_prefiltered.SampleLevel(g_environmentSampler, EnvironmentDirection(reflected),
                                                              surface.roughness * prefilteredMaxLod).rgb;
            const float2 brdf = g_brdfLut.SampleLevel(g_environmentSampler, float2(nDotV, surface.roughness), 0).rg;
            specular += radiance * (surface.f0 * brdf.x + brdf.y) * surface.occlusion * environmentIntensity;
        }
        return;
    }

    diffuse += AmbientIrradiance(surface.normal) * surface.diffuseColor * surface.occlusion;

    // Reflections of the same hemisphere, seen along the mirror direction. Without them a metal,
    // which has no diffuse colour, is black wherever no light's highlight falls.
    if (lightingModel == LIGHTING_MODEL_COOK_TORRANCE)
    {
        const float nDotV = max(dot(surface.normal, surface.view), 1e-4);
        const float3 reflected = reflect(-surface.view, surface.normal);
        const float3 reflectedAmbient = lerp(ambientGround, ambientSky, reflected.y * 0.5 + 0.5);
        specular += reflectedAmbient * EnvBrdfApprox(surface.f0, surface.roughness, nDotV) * surface.occlusion;
    }
}

#endif

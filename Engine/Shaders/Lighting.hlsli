// Lights and the reflectance models that shade them. Units follow glTF's KHR_lights_punctual:
// directional lights give illuminance in lux, point and spot lights intensity in candela, so the
// light arriving at a surface is radiance * attenuation, and what leaves toward the eye is
// BRDF * incoming * cos(angle to the normal).
//
// Mirrored by Engine/Source/Renderer/Lighting.h; change both together.

#ifndef ARCLIGHT_LIGHTING_HLSLI
#define ARCLIGHT_LIGHTING_HLSLI

#include "Common.hlsli"

#define LIGHT_DIRECTIONAL 0
#define LIGHT_POINT 1
#define LIGHT_SPOT 2

#define LIGHTING_MODEL_LAMBERT 0
#define LIGHTING_MODEL_PHONG 1
#define LIGHTING_MODEL_BLINN_PHONG 2
#define LIGHTING_MODEL_COOK_TORRANCE 3

#define LIGHTING_FLAG_ENERGY_CONSERVING 1
#define LIGHTING_FLAG_FRESNEL 2

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
};

// t8 leaves t0..t7 for material and environment textures (structured buffers cannot use t0..t3,
// whose Metal buffer indices belong to b0..b3).
StructuredBuffer<Light> g_lights : register(t8);

// A surface point, ready to be lit.
struct Surface
{
    float3 position;       // World space.
    float3 normal;         // World space, unit length, facing the viewer's side.
    float3 view;           // Unit vector from the surface toward the camera.
    float3 diffuseColor;   // Albedo of the diffuse part: base colour, zero for metals.
    float3 f0;             // Specular reflectance at normal incidence: 0.04 for dielectrics, base colour for metals.
    float roughness;       // Perceptual, 0 to 1, as in glTF.
};

// Schlick's approximation of Fresnel reflectance: f0 at normal incidence, rising to 1 at grazing.
float3 FresnelSchlick(float3 f0, float cosTheta)
{
    const float m = 1.0 - saturate(cosTheta);
    const float m2 = m * m;
    return f0 + (1.0 - f0) * (m2 * m2 * m);
}

// Below this roughness a point light's GGX highlight shrinks to a sub-pixel spike that flickers as
// the surface moves (Filament clamps at the same value).
static const float kMinRoughness = 0.045;

// GGX (Trowbridge-Reitz) normal distribution: the density of microfacets whose normal is the half
// vector. alpha = roughness^2, so roughness reads perceptually linear. Integrates to 1 over the
// hemisphere when weighted by cos(normal, half vector).
float DistributionGgx(float nDotH, float alpha)
{
    const float a2 = alpha * alpha;
    const float d = nDotH * nDotH * (a2 - 1.0) + 1.0;
    return a2 / (PI * d * d);
}

// Smith's height-correlated masking-shadowing for GGX, already divided by the 4 cos(n,l) cos(n,v)
// of the Cook-Torrance denominator (Heitz 2014): the fraction of facets both lit and seen.
float VisibilitySmithGgxCorrelated(float nDotV, float nDotL, float alpha)
{
    const float a2 = alpha * alpha;
    const float v = nDotL * sqrt(nDotV * nDotV * (1.0 - a2) + a2);
    const float l = nDotV * sqrt(nDotL * nDotL * (1.0 - a2) + a2);
    return 0.5 / max(v + l, 1e-7);
}

// The specular BRDF integrated over every light direction under a uniform environment, as the
// split-sum's second term (f0 * A + B), fitted analytically instead of read from a lookup table
// (Karis, "Physically Based Shading on Mobile", 2014). Lights the hemisphere ambient's
// reflections until 4.4's BRDF table replaces it.
float3 EnvBrdfApprox(float3 f0, float roughness, float nDotV)
{
    const float4 c0 = float4(-1.0, -0.0275, -0.572, 0.022);
    const float4 c1 = float4(1.0, 0.0425, 1.04, -0.04);
    const float4 r = roughness * c0 + c1;
    const float a004 = min(r.x * r.x, exp2(-9.28 * nDotV)) * r.x + r.y;
    const float2 ab = float2(-1.04, 1.04) * a004 + r.zw;
    return f0 * ab.x + ab.y;
}

// The Blinn-Phong exponent equivalent to a roughness, through GGX's alpha = roughness^2:
// n = 2 / alpha^2 - 2 gives a Blinn-Phong lobe of about the same width as GGX's (Walter et al.
// 2007). Clamped so roughness 0 stays finite.
float BlinnPhongExponent(float roughness)
{
    const float alpha = max(roughness * roughness, 0.01);
    return clamp(2.0 / (alpha * alpha) - 2.0, 1.0, 8192.0);
}

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

// Every light in the list, plus the hemisphere ambient.
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

    const float3 ambient = lerp(ambientGround, ambientSky, surface.normal.y * 0.5 + 0.5);
    diffuse += ambient * surface.diffuseColor;

    // Reflections of the same hemisphere, seen along the mirror direction. Without them a metal,
    // which has no diffuse colour, is black wherever no light's highlight falls.
    if (lightingModel == LIGHTING_MODEL_COOK_TORRANCE)
    {
        const float nDotV = max(dot(surface.normal, surface.view), 1e-4);
        const float3 reflected = reflect(-surface.view, surface.normal);
        const float3 reflectedAmbient = lerp(ambientGround, ambientSky, reflected.y * 0.5 + 0.5);
        specular += reflectedAmbient * EnvBrdfApprox(surface.f0, surface.roughness, nDotV);
    }
}

#endif

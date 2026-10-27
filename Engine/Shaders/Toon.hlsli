// Cel shading (ShadingModel::Toon): the same lights and ambient light as Lighting.hlsli, with the
// smooth terms of the physical model replaced by flat ones. Diffuse light falls in bands (or is read
// from a ramp texture), the highlight is a hard-edged shape, and a rim of light outlines the lit side
// of the silhouette. Light still arrives in its physical units, so toon and physical surfaces share
// one scene, exposure and tone curve.

#ifndef ARCLIGHT_TOON_HLSLI
#define ARCLIGHT_TOON_HLSLI

#include "Lighting.hlsli"

#define TOON_FLAG_RAMP 1

// Indexed by cos(normal, light) * 0.5 + 0.5 across its width; sampled with the clamped environment
// sampler, so neither end wraps around to the other.
Texture2D<float4> g_toonRamp : register(t9);

struct ToonParams
{
    uint bands;          // Steps of light above shadow, at least 1.
    float softness;      // Half-width of each step's edge, in cos(normal, light).
    float specular;      // Highlight strength; 0 for none.
    float rimSize;       // 0 to 1: how far the rim reaches in from the silhouette.
    float rim;           // Rim strength; 0 for none.
    bool ramp;           // Read light from g_toonRamp instead of cutting bands.
};

// smoothstep() divides by its edges' distance, which a softness of 0 would make zero.
float SoftStep(float edge, float softness, float x)
{
    const float halfWidth = max(softness, 1e-4);
    return smoothstep(edge - halfWidth, edge + halfWidth, x);
}

// The fraction of a light's radiance that lights the surface, in place of the physical cos(n, l):
// edges at cos = 0, 1/bands, 2/bands ..., so one band is plain lit-or-not.
float3 ToonDiffuse(ToonParams params, float nDotL)
{
    if (params.ramp)
        return g_toonRamp.SampleLevel(g_environmentSampler, float2(nDotL * 0.5 + 0.5, 0.5), 0).rgb;
    float tone = 0.0;
    for (uint band = 0; band < params.bands; ++band)
        tone += SoftStep(float(band) / float(params.bands), params.softness, nDotL);
    return (tone / float(params.bands)).xxx;
}

// `surface.diffuseColor` is lit by the bands; `highlightColor` tints the highlight and rim.
void ShadeToon(Surface surface, ToonParams params, float3 highlightColor, out float3 diffuse, out float3 specular)
{
    diffuse = 0.0;
    specular = 0.0;
    const float nDotV = saturate(dot(surface.normal, surface.view));
    // Blinn-Phong's lobe for the material's roughness, cut at half its peak: rough surfaces get a
    // broad patch of highlight, smooth ones a small dot. A rough surface's highlight is also faint,
    // as a physical one would be, or plaster and wood shine like plastic with a broad white patch.
    const float exponent = BlinnPhongExponent(max(surface.roughness, 0.2));
    const float smoothness = 1.0 - saturate(surface.roughness);
    const float highlightStrength = params.specular * smoothness * smoothness;
    const float rimEdge = 1.0 - params.rimSize;
    for (uint i = 0; i < lightCount; ++i)
    {
        const Light light = g_lights[i];
        float3 toLight;
        const float attenuation = LightAttenuation(light, surface.position, toLight);
        if (attenuation <= 0.0)
            continue;
        // The same scale as a white Lambertian surface fully facing the light: radiance / pi.
        const float3 incoming = light.radiance * attenuation / PI;
        const float nDotL = dot(surface.normal, toLight);
        diffuse += surface.diffuseColor * incoming * ToonDiffuse(params, nDotL);

        const float lit = SoftStep(0.0, params.softness, nDotL);
        const float3 halfVector = normalize(toLight + surface.view);
        const float lobe = pow(saturate(dot(surface.normal, halfVector)), exponent);
        const float highlight = SoftStep(0.5, params.softness, lobe) * lit;
        // Strongest where the surface turns away from the viewer, on the side facing the light; the
        // tenth root keeps it from fading early across the lit side.
        const float rimAmount = (1.0 - nDotV) * pow(saturate(nDotL), 0.1);
        const float rim = SoftStep(rimEdge, params.softness, rimAmount);
        specular += highlightColor * incoming * (highlight * highlightStrength + rim * params.rim);
    }
    // Flat ambient light, so the shadow side shows the sky's colour rather than black.
    diffuse += AmbientIrradiance(surface.normal) * surface.diffuseColor * surface.occlusion;
}

#endif

// The reflectance functions shared by the lighting and the image-based lighting precomputation.
// Mirrored for tests by Engine/Source/Renderer/Brdf.h; change both together.

#ifndef ARCLIGHT_BRDF_HLSLI
#define ARCLIGHT_BRDF_HLSLI

#include "Common.hlsli"

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
// reflections; image-based lighting reads the integrated table (IblBrdfLut.hlsl) instead.
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

// The i-th of n points of the Hammersley set: evenly spread over [0, 1)^2, with the second
// coordinate the bit-reversed index (the radical inverse in base 2). Low-discrepancy points give a
// smoother estimate than random ones for the same number of samples.
float2 Hammersley(uint i, uint n)
{
    return float2((float)i / (float)n, (float)reversebits(i) * 2.3283064365386963e-10);
}

// A half vector drawn from the GGX distribution around `normal`, for importance sampling: more
// samples where the lobe is large. Its probability density over half vectors is D * cos(n, h).
float3 ImportanceSampleGgx(float2 xi, float3 normal, float alpha)
{
    const float a2 = alpha * alpha;
    const float phi = 2.0 * PI * xi.x;
    const float cosTheta = sqrt((1.0 - xi.y) / (1.0 + (a2 - 1.0) * xi.y));
    const float sinTheta = sqrt(1.0 - cosTheta * cosTheta);
    const float3 h = float3(sinTheta * cos(phi), sinTheta * sin(phi), cosTheta);

    const float3 up = abs(normal.z) < 0.999 ? float3(0.0, 0.0, 1.0) : float3(1.0, 0.0, 0.0);
    const float3 tangent = normalize(cross(up, normal));
    const float3 bitangent = cross(normal, tangent);
    return normalize(tangent * h.x + bitangent * h.y + normal * h.z);
}

#endif

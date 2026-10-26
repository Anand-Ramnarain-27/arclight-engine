// One mip level of the prefiltered cube: the environment convolved with the GGX lobe of this level's
// roughness, assuming the view is along the reflection (the split-sum's first term). Rough levels
// are blurry, so they are small mips; the lighting reads mip roughness * (levels - 1).

#include "Ibl.hlsli"

TextureCube<float4> g_environment : register(t0);
SamplerState g_sampler : register(s0);   // Trilinear, clamped.

float4 PSMain(VSOutput input) : SV_Target0
{
    const float3 normal = normalize(CubeFaceDirection(face, input.st.x, input.st.y));
    // A mirror reflects the environment itself, read at this level's own resolution.
    if (roughness <= 0.0)
        return float4(g_environment.SampleLevel(g_sampler, normal, sourceLod).rgb, 1.0);

    const float alpha = roughness * roughness;
    // The solid angle of one texel of the source's mip 0.
    const float texelSolidAngle = 4.0 * PI / (6.0 * sourceSize * sourceSize);
    float3 sum = 0.0;
    float weight = 0.0;
    for (uint i = 0; i < sampleCount; ++i)
    {
        const float3 h = ImportanceSampleGgx(Hammersley(i, sampleCount), normal, alpha);
        const float3 l = reflect(-normal, h);
        const float nDotL = dot(normal, l);
        if (nDotL <= 0.0)
            continue;
        // Filtered importance sampling (Colbert and Krivanek, GPU Gems 3 ch. 20): each sample stands
        // for a solid angle of 1 / (count * pdf); reading the mip whose texels cover that much
        // replaces the noise of a few hundred point samples with a smooth average.
        const float nDotH = saturate(dot(normal, h));
        const float pdf = DistributionGgx(nDotH, alpha) * 0.25;   // D cos(n,h) / (4 cos(v,h)), with v = n.
        const float sampleSolidAngle = 1.0 / (float(sampleCount) * pdf + 1e-6);
        const float lod = max(0.5 * log2(sampleSolidAngle / texelSolidAngle) + 1.0, 0.0);
        sum += g_environment.SampleLevel(g_sampler, l, lod).rgb * nDotL;
        weight += nDotL;
    }
    return float4(sum / max(weight, 1e-6), 1.0);
}

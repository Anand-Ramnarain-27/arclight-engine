// The split-sum's second term: the specular BRDF integrated over the hemisphere under white light,
// as f0 * A + B, for every view angle (u = cos(n, v)) and roughness (v). Depends on nothing but the
// BRDF, so it is made once at start-up. Uses the same GGX and height-correlated Smith terms as the
// direct lighting, so the two agree.

#include "Ibl.hlsli"

float4 PSMain(VSOutput input) : SV_Target0
{
    const float nDotV = max((input.st.x + 1.0) * 0.5, 1e-3);
    const float rough = (input.st.y + 1.0) * 0.5;
    const float alpha = max(rough * rough, kMinRoughness * kMinRoughness);
    const float3 normal = float3(0.0, 0.0, 1.0);
    const float3 view = float3(sqrt(1.0 - nDotV * nDotV), 0.0, nDotV);

    float a = 0.0;
    float b = 0.0;
    for (uint i = 0; i < sampleCount; ++i)
    {
        const float3 h = ImportanceSampleGgx(Hammersley(i, sampleCount), normal, alpha);
        const float3 l = reflect(-view, h);
        const float nDotL = l.z;
        if (nDotL <= 0.0)
            continue;
        const float nDotH = saturate(h.z);
        const float vDotH = saturate(dot(view, h));
        // The BRDF times cos(n, l) over the half-vector pdf D cos(n,h) / (4 cos(v,h)): D cancels.
        const float weight = VisibilitySmithGgxCorrelated(nDotV, nDotL, alpha) * 4.0 * nDotL * vDotH / max(nDotH, 1e-6);
        const float fresnel = pow(1.0 - vDotH, 5.0);
        a += (1.0 - fresnel) * weight;
        b += fresnel * weight;
    }
    return float4(a / float(sampleCount), b / float(sampleCount), 0.0, 1.0);
}

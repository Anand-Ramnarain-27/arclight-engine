// One face of the environment cube from another cube texture: the six images of a skybox folder or
// the procedural sky, which are 8-bit sRGB, copied into the half-float cube the lighting is made from.

#include "Ibl.hlsli"

TextureCube<float4> g_source : register(t0);
SamplerState g_sampler : register(s0);   // Trilinear, clamped.

float4 PSMain(VSOutput input) : SV_Target0
{
    const float3 direction = CubeFaceDirection(face, input.st.x, input.st.y);
    return float4(g_source.SampleLevel(g_sampler, direction, sourceLod).rgb, 1.0);
}

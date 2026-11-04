// One face of the environment cube from an equirectangular (latitude-longitude) HDR image, as most
// published HDR skies are stored. The image's centre column faces -Z (forward), +X is to the right,
// and its top row is straight up.

#include "Ibl.hlsli"

Texture2D<float4> g_equirect : register(t0);
SamplerState g_sampler : register(s0);   // Linear, repeating across the image's left and right edges.

float4 PSMain(VSOutput input) : SV_Target0
{
    const float3 cube = normalize(CubeFaceDirection(face, input.st.x, input.st.y));
    const float3 world = float3(cube.x, cube.y, -cube.z);
    const float u = 0.5 + atan2(world.x, -world.z) / (2.0 * PI);
    const float v = acos(clamp(world.y, -1.0, 1.0)) / PI;
    // An explicit level: the image is read at about the cube's own resolution, so smaller mips of the
    // cube average more of the image instead of aliasing.
    return float4(g_equirect.SampleLevel(g_sampler, float2(u, v), sourceLod).rgb, 1.0);
}

// The sky: a full-screen triangle at the far plane that samples a cube texture with each pixel's
// view direction. Depth-tested but not written, so it only covers pixels nothing has drawn to, and
// is drawn after the opaque meshes so those pixels are never shaded twice.

#include "Constants.hlsli"

// Mirrored by SkyboxConstants in Engine/Source/Renderer/Skybox.cpp.
cbuffer SkyboxConstants : register(b1)
{
    float farDepth;    // NDC depth of the far plane: 1, or 0 with reversed-Z.
    float intensity;   // Multiplies the sky's colour.
};

TextureCube<float4> g_sky : register(t0);
SamplerState g_sampler : register(s0);

struct VSOutput
{
    float4 position : SV_Position;
    float2 ndc : TEXCOORD0;
};

VSOutput VSMain(uint vertexId : SV_VertexID)
{
    const float2 uv = float2((vertexId << 1) & 2, vertexId & 2);
    VSOutput output;
    output.ndc = uv * 2.0 - 1.0;
    output.position = float4(output.ndc, farDepth, 1.0);
    return output;
}

float4 PSMain(VSOutput input) : SV_Target0
{
    // Any depth strictly inside the view volume gives a point along the pixel's ray.
    const float4 world = mul(inverseViewProjection, float4(input.ndc, 0.5, 1.0));
    const float3 direction = normalize(world.xyz / world.w - cameraPosition);
    // Cube faces are laid out for a left-handed space with +Z forward; the world is right-handed
    // with -Z forward, so Z is flipped to see the +Z face straight ahead.
    const float3 color = g_sky.SampleLevel(g_sampler, float3(direction.x, direction.y, -direction.z), 0).rgb;
    return float4(color * intensity, 1.0);
}

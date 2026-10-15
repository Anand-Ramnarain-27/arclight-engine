// A textured surface. Positions arrive already projected to clip space but keep their w, so the
// rasterizer still interpolates texture coordinates with perspective correction.

#include "Common.hlsli"

Texture2D<float4> g_texture : register(t0);
SamplerState g_sampler : register(s0);

struct VSInput
{
    float4 position : POSITION;
    float2 uv : TEXCOORD0;
};

struct VSOutput
{
    float4 position : SV_Position;
    float2 uv : TEXCOORD0;
};

VSOutput VSMain(VSInput input)
{
    VSOutput output;
    output.position = input.position;
    output.uv = input.uv;
    return output;
}

float4 PSMain(VSOutput input) : SV_Target0
{
    // Sampling an sRGB texture returns linear colour; the hardware decodes before filtering.
    const float4 color = g_texture.Sample(g_sampler, input.uv);
    return float4(LinearToSrgb(color.rgb), color.a);
}

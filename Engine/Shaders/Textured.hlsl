// A textured surface in world space, transformed by the camera in b0 and the object in b1.

#include "Constants.hlsli"

// Mirrored by TexturedDrawConstants in Editor/Source/Scene/TextureDemoModule.cpp.
cbuffer DrawConstants : register(b1)
{
    float4x4 world;      // Object space to world space.
    float2 uvScale;      // Applied to the mesh's texture coordinates before uvOffset.
    float2 uvOffset;
    float mipBias;       // Added to the mip level the hardware selects; negative is sharper.
};

Texture2D<float4> g_texture : register(t0);
SamplerState g_sampler : register(s0);

struct VSInput
{
    float3 position : POSITION;
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
    output.position = mul(viewProjection, mul(world, float4(input.position, 1.0)));
    output.uv = input.uv * uvScale + uvOffset;
    return output;
}

float4 PSMain(VSOutput input) : SV_Target0
{
    // Sampling an sRGB texture returns linear colour, which is what the HDR scene target holds;
    // the hardware decodes before filtering.
    return g_texture.SampleBias(g_sampler, input.uv, mipBias);
}

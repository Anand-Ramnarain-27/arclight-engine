// Turns the HDR scene (linear light, any brightness) into the display target (sRGB-encoded, 0 to 1):
// exposure, then a tone curve that rolls highlights off instead of clipping them, then the sRGB
// transfer function. One full-screen triangle; the two targets are the same size, so pixels are
// read with Load() and need no sampler.

#include "Common.hlsli"

// Mirrored by ToneMapConstants in Engine/Source/Renderer/ToneMapper.cpp.
cbuffer ToneMapConstants : register(b1)
{
    float exposureScale;   // 2^EV, applied before the curve.
    uint toneOperator;     // One of the TONE_ values below.
    uint passThrough;      // Non-zero: no exposure or curve, for debug views that show data.
};

// Match arc::ToneMapOperator.
#define TONE_CLAMP 0
#define TONE_REINHARD 1
#define TONE_ACES 2
#define TONE_PBR_NEUTRAL 3

Texture2D<float4> g_scene : register(t0);

float4 VSMain(uint vertexId : SV_VertexID) : SV_Position
{
    const float2 uv = float2((vertexId << 1) & 2, vertexId & 2);
    return float4(uv * 2.0 - 1.0, 0.5, 1.0);
}

// Per channel x / (1 + x): never reaches 1, and desaturates bright colours toward white only as
// fast as the channels approach 1 at different rates.
float3 Reinhard(float3 color)
{
    return color / (1.0 + color);
}

// Stephen Hill's fit of the ACES reference rendering and output transforms (BakingLab, MIT). The
// matrices convert linear sRGB to and from the ACES working space the fit was made in, and fold in
// the RRT's saturation adjustment.
static const float3x3 kAcesInput = {
    {0.59719, 0.35458, 0.04823},
    {0.07600, 0.90834, 0.01566},
    {0.02840, 0.13383, 0.83777},
};
static const float3x3 kAcesOutput = {
    {1.60475, -0.53108, -0.07367},
    {-0.10208, 1.10813, -0.00605},
    {-0.00327, -0.07276, 1.07602},
};

float3 Aces(float3 color)
{
    color = mul(kAcesInput, color);
    const float3 a = color * (color + 0.0245786) - 0.000090537;
    const float3 b = color * (0.983729 * color + 0.4329510) + 0.238081;
    color = mul(kAcesOutput, a / b);
    return saturate(color);
}

// Khronos PBR Neutral (https://github.com/KhronosGroup/ToneMapping): colours below 0.76 pass through
// almost unchanged, so a base colour of 0.5 under a white light of 1 shows as 0.5. Only the
// brightest channel is compressed, with the hue kept, then desaturated toward white.
float3 PbrNeutral(float3 color)
{
    const float startCompression = 0.8 - 0.04;
    const float desaturation = 0.15;

    const float x = min(color.r, min(color.g, color.b));
    const float offset = x < 0.08 ? x - 6.25 * x * x : 0.04;
    color -= offset;

    const float peak = max(color.r, max(color.g, color.b));
    if (peak < startCompression)
        return color;

    const float d = 1.0 - startCompression;
    const float newPeak = 1.0 - d * d / (peak + d - startCompression);
    color *= newPeak / peak;
    const float g = 1.0 - 1.0 / (desaturation * (peak - newPeak) + 1.0);
    return lerp(color, newPeak.xxx, g);
}

float4 PSMain(float4 position : SV_Position) : SV_Target0
{
    // Negative values and NaNs (from a shader dividing by zero) would spread through the curves;
    // the 16-bit float target already caps values at 65504.
    float3 color = g_scene.Load(int3(position.xy, 0)).rgb;
    color = select(color > 0.0, color, 0.0);

    if (passThrough == 0)
    {
        color *= exposureScale;
        if (toneOperator == TONE_REINHARD)
            color = Reinhard(color);
        else if (toneOperator == TONE_ACES)
            color = Aces(color);
        else if (toneOperator == TONE_PBR_NEUTRAL)
            color = PbrNeutral(color);
    }
    return float4(LinearToSrgb(color), 1.0);
}

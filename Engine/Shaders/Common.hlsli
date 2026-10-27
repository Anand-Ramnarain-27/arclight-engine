// Helpers shared by the shaders in this folder.

#ifndef ARCLIGHT_COMMON_HLSLI
#define ARCLIGHT_COMMON_HLSLI

static const float PI = 3.14159265358979;

// The brightest value written to the RGBA16F scene target, just under half floats' largest (65504).
// A brighter value would be stored as infinity, which the tone curves turn into NaN, shown as black:
// a mirror-smooth surface reflecting the sun, or a point light's highlight at roughness 0, reaches it.
static const float kMaxSceneRadiance = 60000.0;

// Finite and in range for the scene target. max() returns its other operand for NaN on D3D12 and
// Metal alike, so a NaN from a degenerate input becomes 0 instead of spreading.
float3 SceneRadiance(float3 color)
{
    return min(max(color, 0.0), kMaxSceneRadiance);
}

// The sRGB transfer function (IEC 61966-2-1), linear to encoded. The display target and the
// backbuffer have UNORM formats, which store values as written, so the tone-mapping pass encodes
// colour itself. Expects colour in [0, 1].
float3 LinearToSrgb(float3 color)
{
    color = saturate(color);
    const float3 low = color * 12.92;
    const float3 high = 1.055 * pow(color, 1.0 / 2.4) - 0.055;
    return select(color <= 0.0031308, low, high);
}

// The inverse: encoded to linear. For values meant to be shown exactly as they are, such as debug
// views of normals, which the tone-mapping pass will encode again.
float3 SrgbToLinear(float3 color)
{
    color = saturate(color);
    const float3 low = color / 12.92;
    const float3 high = pow((color + 0.055) / 1.055, 2.4);
    return select(color <= 0.04045, low, high);
}

#endif

// Helpers shared by the shaders in this folder.

#ifndef ARCLIGHT_COMMON_HLSLI
#define ARCLIGHT_COMMON_HLSLI

static const float PI = 3.14159265358979;

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

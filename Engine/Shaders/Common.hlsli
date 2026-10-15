// Helpers shared by the shaders in this folder.

#ifndef ARCLIGHT_COMMON_HLSLI
#define ARCLIGHT_COMMON_HLSLI

// The sRGB transfer function (IEC 61966-2-1), linear to encoded. The backbuffer has a UNORM
// format, which stores values as written, so a shader that computes linear colour encodes it
// itself. Expects colour in [0, 1].
float3 LinearToSrgb(float3 color)
{
    color = saturate(color);
    const float3 low = color * 12.92;
    const float3 high = 1.055 * pow(color, 1.0 / 2.4) - 0.055;
    return select(color <= 0.0031308, low, high);
}

#endif

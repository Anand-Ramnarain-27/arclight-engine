#pragma once

#include "Math/Vector.h"

#include <algorithm>
#include <cmath>

namespace arc
{
    // The sRGB transfer function (IEC 61966-2-1), as GPUs apply it to sRGB formats. Colour is
    // stored and computed linear; sRGB encoding is for images and for what people pick in colour
    // editors. Values are clamped to [0, 1].

    [[nodiscard]] inline float srgbToLinear(float encoded)
    {
        const float c = std::clamp(encoded, 0.0f, 1.0f);
        return c <= 0.04045f ? c / 12.92f : std::pow((c + 0.055f) / 1.055f, 2.4f);
    }

    [[nodiscard]] inline float linearToSrgb(float linear)
    {
        const float c = std::clamp(linear, 0.0f, 1.0f);
        return c <= 0.0031308f ? c * 12.92f : 1.055f * std::pow(c, 1.0f / 2.4f) - 0.055f;
    }

    [[nodiscard]] inline Vec3 srgbToLinear(const Vec3& encoded)
    {
        return {srgbToLinear(encoded.x), srgbToLinear(encoded.y), srgbToLinear(encoded.z)};
    }

    [[nodiscard]] inline Vec3 linearToSrgb(const Vec3& linear)
    {
        return {linearToSrgb(linear.x), linearToSrgb(linear.y), linearToSrgb(linear.z)};
    }
}

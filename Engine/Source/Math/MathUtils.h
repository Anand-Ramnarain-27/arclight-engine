#pragma once

#include <algorithm>
#include <cmath>

namespace arc
{
    inline constexpr float kPi = 3.14159265358979323846f;
    inline constexpr float kTwoPi = 2.0f * kPi;
    inline constexpr float kHalfPi = 0.5f * kPi;

    /// Default tolerance for comparisons of normalised or unit-scale values.
    inline constexpr float kEpsilon = 1e-5f;

    [[nodiscard]] constexpr float toRadians(float degrees) { return degrees * (kPi / 180.0f); }
    [[nodiscard]] constexpr float toDegrees(float radians) { return radians * (180.0f / kPi); }

    [[nodiscard]] constexpr float lerp(float a, float b, float t) { return a + (b - a) * t; }

    [[nodiscard]] constexpr float saturate(float value) { return std::clamp(value, 0.0f, 1.0f); }

    [[nodiscard]] inline bool nearlyEqual(float a, float b, float epsilon = kEpsilon)
    {
        return std::abs(a - b) <= epsilon;
    }
}

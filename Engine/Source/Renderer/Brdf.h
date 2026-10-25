#pragma once

#include "Math/Math.h"

#include <algorithm>
#include <cmath>

namespace arc::brdf
{
    // CPU copies of the reflectance functions in Engine/Shaders/Lighting.hlsli, for tests that
    // integrate them numerically (a lobe that does not integrate to what it should is a wrong
    // shader). Change both together.

    /// Schlick's Fresnel for one channel: f0 at normal incidence, 1 at grazing.
    [[nodiscard]] inline float fresnelSchlick(float f0, float cosTheta)
    {
        const float m = 1.0f - std::clamp(cosTheta, 0.0f, 1.0f);
        return f0 + (1.0f - f0) * (m * m * m * m * m);
    }

    /// GGX normal distribution; alpha = roughness^2.
    [[nodiscard]] inline float distributionGgx(float nDotH, float alpha)
    {
        const float a2 = alpha * alpha;
        const float d = nDotH * nDotH * (a2 - 1.0f) + 1.0f;
        return a2 / (kPi * d * d);
    }

    /// Smith height-correlated masking-shadowing over 4 cos(n,l) cos(n,v).
    [[nodiscard]] inline float visibilitySmithGgxCorrelated(float nDotV, float nDotL, float alpha)
    {
        const float a2 = alpha * alpha;
        const float v = nDotL * std::sqrt(nDotV * nDotV * (1.0f - a2) + a2);
        const float l = nDotV * std::sqrt(nDotL * nDotL * (1.0f - a2) + a2);
        return 0.5f / std::max(v + l, 1e-7f);
    }

    /// The Blinn-Phong exponent matching a roughness through alpha = roughness^2.
    [[nodiscard]] inline float blinnPhongExponent(float roughness)
    {
        const float alpha = std::max(roughness * roughness, 0.01f);
        return std::clamp(2.0f / (alpha * alpha) - 2.0f, 1.0f, 8192.0f);
    }

    /// Karis' analytic fit of the split-sum environment BRDF: the specular reflectance f0 * A + B
    /// of a surface under uniform light, for one channel.
    [[nodiscard]] inline float envBrdfApprox(float f0, float roughness, float nDotV)
    {
        const float r0 = roughness * -1.0f + 1.0f;
        const float r1 = roughness * -0.0275f + 0.0425f;
        const float r2 = roughness * -0.572f + 1.04f;
        const float r3 = roughness * 0.022f - 0.04f;
        const float a004 = std::min(r0 * r0, std::exp2(-9.28f * nDotV)) * r0 + r1;
        const float a = -1.04f * a004 + r2;
        const float b = 1.04f * a004 + r3;
        return f0 * a + b;
    }
}

#pragma once

#include "Math/Math.h"

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace arc::brdf
{
    // CPU copies of the reflectance functions in Engine/Shaders/Brdf.hlsli, for tests that
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

    /// Hammersley point i of n: (i / n, the bit-reversed i as a fraction).
    [[nodiscard]] inline Vec2 hammersley(uint32_t i, uint32_t n)
    {
        uint32_t bits = i;
        bits = (bits << 16) | (bits >> 16);
        bits = ((bits & 0x55555555u) << 1) | ((bits & 0xAAAAAAAAu) >> 1);
        bits = ((bits & 0x33333333u) << 2) | ((bits & 0xCCCCCCCCu) >> 2);
        bits = ((bits & 0x0F0F0F0Fu) << 4) | ((bits & 0xF0F0F0F0u) >> 4);
        bits = ((bits & 0x00FF00FFu) << 8) | ((bits & 0xFF00FF00u) >> 8);
        return {static_cast<float>(i) / static_cast<float>(n), static_cast<float>(bits) * 2.3283064365386963e-10f};
    }

    /// A GGX-distributed half vector around `normal`, with density D * cos(n, h).
    [[nodiscard]] inline Vec3 importanceSampleGgx(Vec2 xi, const Vec3& normal, float alpha)
    {
        const float a2 = alpha * alpha;
        const float phi = kTwoPi * xi.x;
        const float cosTheta = std::sqrt((1.0f - xi.y) / (1.0f + (a2 - 1.0f) * xi.y));
        const float sinTheta = std::sqrt(1.0f - cosTheta * cosTheta);
        const Vec3 up = std::abs(normal.z) < 0.999f ? Vec3{0.0f, 0.0f, 1.0f} : Vec3{1.0f, 0.0f, 0.0f};
        const Vec3 tangent = normalize(cross(up, normal));
        const Vec3 bitangent = cross(normal, tangent);
        return normalize(tangent * (sinTheta * std::cos(phi)) + bitangent * (sinTheta * std::sin(phi)) + normal * cosTheta);
    }

    /// The split-sum BRDF table's entry, (A, B) with reflectance f0 * A + B, by importance sampling
    /// as IblBrdfLut.hlsl does.
    [[nodiscard]] inline Vec2 integrateEnvBrdf(float nDotV, float roughness, uint32_t sampleCount)
    {
        constexpr float kMinRoughness = 0.045f;
        nDotV = std::max(nDotV, 1e-3f);
        const float alpha = std::max(roughness * roughness, kMinRoughness * kMinRoughness);
        const Vec3 normal{0.0f, 0.0f, 1.0f};
        const Vec3 view{std::sqrt(1.0f - nDotV * nDotV), 0.0f, nDotV};
        float a = 0.0f;
        float b = 0.0f;
        for (uint32_t i = 0; i < sampleCount; ++i)
        {
            const Vec3 h = importanceSampleGgx(hammersley(i, sampleCount), normal, alpha);
            const Vec3 l = h * (2.0f * dot(view, h)) - view;
            const float nDotL = l.z;
            if (nDotL <= 0.0f)
                continue;
            const float nDotH = std::clamp(h.z, 0.0f, 1.0f);
            const float vDotH = std::clamp(dot(view, h), 0.0f, 1.0f);
            const float weight =
                visibilitySmithGgxCorrelated(nDotV, nDotL, alpha) * 4.0f * nDotL * vDotH / std::max(nDotH, 1e-6f);
            const float fresnel = std::pow(1.0f - vDotH, 5.0f);
            a += (1.0f - fresnel) * weight;
            b += fresnel * weight;
        }
        return {a / static_cast<float>(sampleCount), b / static_cast<float>(sampleCount)};
    }
}

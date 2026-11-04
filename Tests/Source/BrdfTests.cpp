#include "Renderer/Brdf.h"

#include <doctest/doctest.h>

#include <cmath>

using namespace arc;

namespace
{
    // Directional albedo: the fraction of light from every direction above the surface that a BRDF
    // sends toward a viewer at `viewAngle` from the normal, i.e. the integral of BRDF * cos(theta_l)
    // over the hemisphere, by the midpoint rule in spherical coordinates. A physically plausible
    // BRDF never returns more than 1 (the "white furnace" test).
    template <typename Brdf>
    double directionalAlbedo(float viewAngle, Brdf&& brdf, int thetaSteps = 600, int phiSteps = 300)
    {
        const Vec3 normal{0.0f, 0.0f, 1.0f};
        const Vec3 view{std::sin(viewAngle), 0.0f, std::cos(viewAngle)};
        const double dTheta = kHalfPi / thetaSteps;
        const double dPhi = kTwoPi / phiSteps;
        double sum = 0.0;
        for (int i = 0; i < thetaSteps; ++i)
        {
            const double theta = (i + 0.5) * dTheta;
            for (int j = 0; j < phiSteps; ++j)
            {
                const double phi = (j + 0.5) * dPhi;
                const Vec3 light{static_cast<float>(std::sin(theta) * std::cos(phi)),
                                 static_cast<float>(std::sin(theta) * std::sin(phi)), static_cast<float>(std::cos(theta))};
                sum += brdf(normal, view, light) * std::cos(theta) * std::sin(theta) * dTheta * dPhi;
            }
        }
        return sum;
    }

    // Cook-Torrance specular with a given Fresnel, as Lighting.hlsli evaluates it.
    auto ggxSpecular(float roughness, float f0)
    {
        return [roughness, f0](const Vec3& n, const Vec3& v, const Vec3& l) {
            const float alpha = roughness * roughness;
            const Vec3 h = normalize(l + v);
            const float nDotL = dot(n, l);
            const float nDotV = std::max(dot(n, v), 1e-4f);
            const float fresnel = brdf::fresnelSchlick(f0, dot(v, h));
            return brdf::distributionGgx(std::max(dot(n, h), 0.0f), alpha) *
                   brdf::visibilitySmithGgxCorrelated(nDotV, nDotL, alpha) * fresnel;
        };
    }
}

TEST_CASE("Lambert's 1/pi makes a white matte surface reflect exactly what it receives")
{
    const double albedo = directionalAlbedo(0.0f, [](const Vec3&, const Vec3&, const Vec3&) { return 1.0f / kPi; });
    CHECK(albedo == doctest::Approx(1.0).epsilon(1e-3));
}

TEST_CASE("Schlick's Fresnel runs from f0 facing the surface to 1 at grazing angles")
{
    CHECK(brdf::fresnelSchlick(0.04f, 1.0f) == doctest::Approx(0.04f));
    CHECK(brdf::fresnelSchlick(0.04f, 0.0f) == doctest::Approx(1.0f));
    CHECK(brdf::fresnelSchlick(0.04f, 0.5f) > 0.04f);
    CHECK(brdf::fresnelSchlick(0.04f, 0.5f) < 0.1f);
}

TEST_CASE("The GGX distribution integrates to 1 over projected microfacet area")
{
    for (const float roughness : {0.2f, 0.5f, 0.8f, 1.0f})
    {
        CAPTURE(roughness);
        const float alpha = roughness * roughness;
        constexpr int kSteps = 200000;
        const double dTheta = kHalfPi / kSteps;
        double sum = 0.0;
        for (int i = 0; i < kSteps; ++i)
        {
            const double theta = (i + 0.5) * dTheta;
            sum += brdf::distributionGgx(static_cast<float>(std::cos(theta)), alpha) * std::cos(theta) * std::sin(theta) * dTheta;
        }
        CHECK(sum * kTwoPi == doctest::Approx(1.0).epsilon(2e-3));
    }
}

TEST_CASE("Cook-Torrance specular never reflects more than it receives, and loses energy when rough")
{
    // With F = 1 everything a facet receives is reflected; what is missing is light that bounces
    // between facets more than once, which single-scattering Smith ignores.
    const double smooth = directionalAlbedo(0.0f, ggxSpecular(0.3f, 1.0f));
    const double rough = directionalAlbedo(0.0f, ggxSpecular(1.0f, 1.0f));
    const double grazing = directionalAlbedo(toRadians(75.0f), ggxSpecular(0.5f, 1.0f));
    CHECK(smooth <= 1.0 + 1e-3);
    CHECK(smooth > 0.95);
    // At roughness 1, D = 1/pi and V = 1 / (2 (cos l + cos v)), so facing the surface the integral is
    // 1 - ln 2: almost 70% of the light is lost to the missing multiple bounces.
    CHECK(rough == doctest::Approx(1.0 - std::log(2.0)).epsilon(5e-3));
    CHECK(grazing <= 1.0 + 1e-3);
}

TEST_CASE("Normalised Phong and Blinn-Phong keep their energy as the lobe narrows")
{
    for (const float roughness : {0.3f, 0.5f, 0.7f})
    {
        CAPTURE(roughness);
        const float blinn = brdf::blinnPhongExponent(roughness);
        const float phong = std::max(blinn * 0.25f, 1.0f);

        const double phongAlbedo = directionalAlbedo(0.0f, [phong](const Vec3& n, const Vec3& v, const Vec3& l) {
            const Vec3 reflected = n * (2.0f * dot(n, l)) - l;
            return (phong + 2.0f) / kTwoPi * std::pow(std::max(dot(reflected, v), 0.0f), phong);
        });
        const double blinnAlbedo = directionalAlbedo(0.0f, [blinn](const Vec3& n, const Vec3& v, const Vec3& l) {
            const Vec3 h = normalize(l + v);
            return (blinn + 8.0f) / (8.0f * kPi) * std::pow(std::max(dot(n, h), 0.0f), blinn);
        });
        // Phong's factor is exact for a viewer along the normal; Blinn-Phong's is an approximation.
        CHECK(phongAlbedo == doctest::Approx(1.0).epsilon(0.01));
        CHECK(blinnAlbedo == doctest::Approx(1.0).epsilon(0.15));
    }
}

TEST_CASE("The analytic environment BRDF stays close to the integrated split sum")
{
    // f0 = 1 gives the scale plus the bias, f0 = 0 the bias alone.
    for (const float roughness : {0.3f, 0.6f, 0.9f})
    {
        for (const float viewAngle : {0.0f, toRadians(45.0f), toRadians(70.0f)})
        {
            CAPTURE(roughness);
            CAPTURE(viewAngle);
            const float nDotV = std::cos(viewAngle);
            const double white = directionalAlbedo(viewAngle, ggxSpecular(roughness, 1.0f), 400, 200);
            const double black = directionalAlbedo(viewAngle, ggxSpecular(roughness, 0.0f), 400, 200);
            CHECK(brdf::envBrdfApprox(1.0f, roughness, nDotV) == doctest::Approx(white).epsilon(0.12));
            CHECK(std::abs(brdf::envBrdfApprox(0.0f, roughness, nDotV) - black) < 0.05);
        }
    }
}

#pragma once

#include "Math/Math.h"

#include <cstddef>
#include <cstdint>

namespace arc
{
    // What the shaders need to light a scene: the light list (a StructuredBuffer) and the per-frame
    // lighting constants (b3), with their C++ mirrors. Engine/Shaders/Lighting.hlsli is the HLSL side.
    //
    // Units follow glTF's KHR_lights_punctual, so imported lights mean the same thing: directional
    // lights in lux (lm/m^2), point and spot lights in candela (lm/sr), distances in metres.

    /// Values match LIGHT_ in Lighting.hlsli.
    enum class LightType : uint32_t
    {
        /// Infinitely far away, like the sun: one direction, no falloff. Points along its object's -Z.
        Directional = 0,
        /// Shines equally in every direction from its object's position.
        Point = 1,
        /// A point light limited to a cone around its object's -Z.
        Spot = 2,
    };

    /// Also the name stored in scene files.
    [[nodiscard]] const char* lightTypeName(LightType type);

    /// One light as the shaders read it: an element of StructuredBuffer<Light> in Lighting.hlsli.
    /// Structured buffers pack members back to back, but a float3 may not straddle 16 bytes (the
    /// SPIR-V layout DXC uses for Metal), so every float3 is followed by a scalar.
    struct GpuLight
    {
        Vec3 position;            // World space. Unused by directional lights.
        float range = 0.0f;       // Metres at which the light has faded to zero; 0 for no limit.
        Vec3 direction;           // World space, unit length: the way the light travels.
        LightType type = LightType::Directional;
        Vec3 radiance;            // Linear colour times intensity.
        float spotScale = 0.0f;   // Cone fade = saturate(cos(angle) * spotScale + spotOffset)^2.
        float spotOffset = 1.0f;  // 1 with a zero scale: no cone, for point and directional lights.
        float padding[3]{};
    };
    static_assert(offsetof(GpuLight, range) == 12);
    static_assert(offsetof(GpuLight, direction) == 16);
    static_assert(offsetof(GpuLight, type) == 28);
    static_assert(offsetof(GpuLight, radiance) == 32);
    static_assert(offsetof(GpuLight, spotScale) == 44);
    static_assert(offsetof(GpuLight, spotOffset) == 48);
    static_assert(sizeof(GpuLight) == 64);

    /// What a light component describes, before it is placed in the world.
    struct LightDesc
    {
        LightType type = LightType::Point;
        Vec3 color = Vec3::one();       // Linear RGB.
        float intensity = 1.0f;         // Lux for directional lights, candela otherwise.
        float range = 0.0f;             // Metres; 0 for no limit.
        float innerConeAngle = 0.0f;    // Radians from the axis where a spot light starts to fade.
        float outerConeAngle = kPi / 4; // Radians from the axis where it reaches zero; at most pi/2.
    };

    /// `direction` is the way the light travels, and need not be unit length. Out-of-range values
    /// are clamped: negative intensity or range to 0, cone angles to 0 <= inner < outer <= pi/2.
    [[nodiscard]] GpuLight makeGpuLight(const LightDesc& light, const Vec3& position, const Vec3& direction);

    /// The light used when a scene has none, so a model is never shown black: a warm sun from above,
    /// in front and to the right, matching the procedural sky's.
    [[nodiscard]] GpuLight defaultSunLight();

    /// The most lights one frame shades. Every pixel loops over all of them, which is fine for tens
    /// of lights; tiled light culling (Phase 6) is what makes hundreds affordable.
    inline constexpr uint32_t kMaxLights = 256;

    /// Where the light list and the lighting constants are bound: t8 and b3 in Lighting.hlsli.
    inline constexpr uint32_t kLightBufferSlot = 8;
    inline constexpr uint32_t kLightingConstantsSlot = 3;

    /// The specular term the shaders use. Values match LIGHTING_MODEL_ in Lighting.hlsli.
    enum class LightingModel : uint32_t
    {
        /// Diffuse only.
        Lambert = 0,
        /// Specular lobe around the mirror direction: cos(reflected, view)^n.
        Phong = 1,
        /// Specular lobe around the normal: cos(normal, half vector)^n. Closer to measured
        /// materials than Phong, especially at grazing angles, where Phong's highlight stays round.
        BlinnPhong = 2,
        /// Microfacet specular D * G * F / (4 cos cos): the GGX distribution of facet normals, Smith's
        /// height-correlated shadowing-masking and Schlick's Fresnel, as glTF's metallic-roughness
        /// model defines. Normalised by construction.
        CookTorrance = 3,
    };

    [[nodiscard]] const char* lightingModelName(LightingModel model);

    /// Scene-wide lighting choices. Not saved with the scene yet.
    struct LightingSettings
    {
        LightingModel model = LightingModel::CookTorrance;
        /// Phong and Blinn-Phong only: divides the specular lobe by its integral, so a shinier
        /// (narrower) highlight is brighter rather than the surface reflecting more light in total.
        bool energyConserving = true;
        /// Schlick's approximation: reflectance rises toward 1 at grazing angles. Off uses F0 everywhere.
        bool fresnel = true;
        /// A hemisphere of ambient light until image-based lighting replaces it: the sky colour from
        /// above, the ground colour from below. Lights the diffuse colour, and the specular
        /// reflectance through an analytic fit of the split-sum BRDF (see EnvBrdfApprox).
        Vec3 ambientSky{0.30f, 0.34f, 0.42f};
        Vec3 ambientGround{0.12f, 0.10f, 0.09f};
        float ambientIntensity = 1.0f;
        /// Light the scene with defaultSunLight() while it has no lights of its own.
        bool defaultLight = true;
        /// Shade every material with `roughness` / `metallic` instead of its own, to compare lobes.
        bool overrideRoughness = false;
        float roughness = 0.5f;
        bool overrideMetallic = false;
        float metallic = 1.0f;
    };

    /// Mirrors LightingConstants (b3) in Lighting.hlsli.
    struct LightingConstants
    {
        Vec3 ambientSky;
        uint32_t lightCount = 0;
        Vec3 ambientGround;
        uint32_t model = 0;
        uint32_t flags = 0;   // LIGHTING_FLAG_ bits.
        float roughnessOverride = -1.0f;   // Negative: each material's own.
        float metallicOverride = -1.0f;    // Negative: each material's own.
        uint32_t padding = 0;
    };
    static_assert(offsetof(LightingConstants, lightCount) == 12);
    static_assert(offsetof(LightingConstants, ambientGround) == 16);
    static_assert(offsetof(LightingConstants, model) == 28);
    static_assert(offsetof(LightingConstants, flags) == 32);
    static_assert(offsetof(LightingConstants, roughnessOverride) == 36);
    static_assert(offsetof(LightingConstants, metallicOverride) == 40);
    static_assert(sizeof(LightingConstants) == 48);

    /// Match LIGHTING_FLAG_ in Lighting.hlsli.
    inline constexpr uint32_t kLightingFlagEnergyConserving = 1u << 0;
    inline constexpr uint32_t kLightingFlagFresnel = 1u << 1;

    [[nodiscard]] LightingConstants makeLightingConstants(const LightingSettings& settings, uint32_t lightCount);
}

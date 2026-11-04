#pragma once

#include "Math/Math.h"

#include <cstdint>

namespace arc
{
    namespace rhi
    {
        class Texture;
    }

    /// How a material's surface turns light into colour. Values match SHADING_ in Mesh.hlsl.
    enum class ShadingModel : uint32_t
    {
        /// glTF's metallic-roughness, lit by the Lighting menu's reflectance model.
        Physical = 0,
        /// Cel shading (Toon.hlsli): light in flat bands, a hard-edged highlight and a rim of light.
        Toon = 1,
    };

    [[nodiscard]] const char* shadingModelName(ShadingModel model);

    /// The fraction of a light that lights a toon surface without a ramp, for cos(normal, light):
    /// a CPU copy of ToonDiffuse in Engine/Shaders/Toon.hlsli, for tests. Change both together.
    [[nodiscard]] float toonBandLight(float nDotL, uint32_t bands, float softness);

    /// The cel-shading look. Brightness still comes from the lights in their physical units, so a
    /// toon surface sits in the same scene, exposure and tone curve as a physical one.
    struct ToonStyle
    {
        /// Steps of light between shadow and full light: 1 is two-tone (lit or not), 3 is shadow plus
        /// three. Ignored while a ramp texture is set.
        uint32_t bands = 2;
        /// Width of the blend at each band's edge, in cos(normal, light): 0 is a hard step.
        float softness = 0.02f;
        /// A highlight with the material's roughness deciding its size, cut off sharply, and fading
        /// as (1 - roughness)^2 so rough surfaces barely shine. 0 removes it.
        float specular = 0.5f;
        /// How far the rim of light reaches in from the silhouette on the lit side, 0 to 1.
        float rimSize = 0.25f;
        float rim = 0.5f;
        /// Light reaching the surface is looked up across this texture's width by
        /// cos(normal, light) * 0.5 + 0.5 instead of being cut into bands: the left half lights the
        /// side facing away from the light, so a ramp can tint the shadows. Null for none. Not owned.
        rhi::Texture* ramp = nullptr;

        bool operator==(const ToonStyle&) const = default;
    };

    /// Inverted-hull outlines: the mesh drawn again behind itself, grown along its normals and with
    /// its front faces culled, so only a rim shows around the silhouette and creases.
    struct OutlineStyle
    {
        bool enabled = false;
        /// Pixels on screen, the same at any distance.
        float width = 2.0f;
        Vec3 color{0.02f, 0.02f, 0.025f};   // Linear RGB.

        bool operator==(const OutlineStyle&) const = default;
    };

    /// A per-object choice for one material of a model, kept by MeshComponent: the model's own
    /// materials are shared by every object that uses it, so the look of one object lives with the
    /// object.
    struct MaterialStyle
    {
        ShadingModel shading = ShadingModel::Physical;
        ToonStyle toon;
        OutlineStyle outline;

        bool operator==(const MaterialStyle&) const = default;

        /// Draws exactly as the material does without a style.
        [[nodiscard]] bool isDefault() const { return *this == MaterialStyle{}; }
    };
}

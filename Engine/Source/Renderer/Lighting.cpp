#include "Renderer/Lighting.h"

#include <algorithm>
#include <cmath>

namespace arc
{
    const char* lightTypeName(LightType type)
    {
        switch (type)
        {
            case LightType::Directional: return "Directional";
            case LightType::Point:       return "Point";
            case LightType::Spot:        return "Spot";
        }
        return "";
    }

    const char* lightingModelName(LightingModel model)
    {
        switch (model)
        {
            case LightingModel::Lambert:      return "Lambert";
            case LightingModel::Phong:        return "Phong";
            case LightingModel::BlinnPhong:   return "Blinn-Phong";
            case LightingModel::CookTorrance: return "Cook-Torrance (GGX)";
        }
        return "";
    }

    const char* ambientSourceName(AmbientSource source)
    {
        switch (source)
        {
            case AmbientSource::Hemisphere:  return "Hemisphere";
            case AmbientSource::Environment: return "Environment (IBL)";
        }
        return "";
    }

    GpuLight makeGpuLight(const LightDesc& light, const Vec3& position, const Vec3& direction)
    {
        GpuLight result;
        result.type = light.type;
        result.position = position;
        const float directionLength = length(direction);
        result.direction = directionLength > 1e-12f ? direction / directionLength : Vec3{0.0f, -1.0f, 0.0f};
        result.range = light.type == LightType::Directional ? 0.0f : std::max(light.range, 0.0f);
        const Vec3 color{std::max(light.color.x, 0.0f), std::max(light.color.y, 0.0f), std::max(light.color.z, 0.0f)};
        result.radiance = color * std::max(light.intensity, 0.0f);

        if (light.type == LightType::Spot)
        {
            // KHR_lights_punctual's cone: a smooth fade from the inner to the outer angle, done with
            // one multiply-add per pixel on the cosine. The difference of the cosines is kept above a
            // small minimum so the scale stays finite when the two angles are equal.
            const float outer = std::clamp(light.outerConeAngle, 1e-3f, kHalfPi);
            const float inner = std::clamp(light.innerConeAngle, 0.0f, outer);
            const float cosOuter = std::cos(outer);
            const float scale = 1.0f / std::max(std::cos(inner) - cosOuter, 1e-3f);
            result.spotScale = scale;
            result.spotOffset = -cosOuter * scale;
        }
        return result;
    }

    GpuLight defaultSunLight()
    {
        const LightDesc sun{
            .type = LightType::Directional,
            .color = {1.0f, 0.96f, 0.9f},
            .intensity = 3.0f,
        };
        return makeGpuLight(sun, Vec3::zero(), -Vec3{0.4f, 0.8f, 0.45f});
    }

    LightingConstants makeLightingConstants(const LightingSettings& settings, uint32_t lightCount,
                                            const EnvironmentLighting* environment)
    {
        LightingConstants constants;
        constants.ambientSky = settings.ambientSky * settings.ambientIntensity;
        constants.ambientGround = settings.ambientGround * settings.ambientIntensity;
        constants.lightCount = lightCount;
        constants.model = static_cast<uint32_t>(settings.model);
        constants.flags = (settings.energyConserving ? kLightingFlagEnergyConserving : 0u) |
                          (settings.fresnel ? kLightingFlagFresnel : 0u) |
                          (settings.normalMaps ? kLightingFlagNormalMaps : 0u) |
                          (settings.occlusion ? kLightingFlagOcclusion : 0u) | (settings.emissive ? kLightingFlagEmissive : 0u);
        constants.roughnessOverride = settings.overrideRoughness ? std::clamp(settings.roughness, 0.0f, 1.0f) : -1.0f;
        constants.metallicOverride = settings.overrideMetallic ? std::clamp(settings.metallic, 0.0f, 1.0f) : -1.0f;
        if (settings.ambientSource == AmbientSource::Environment && environment && environment->complete())
        {
            constants.flags |= kLightingFlagEnvironment;
            constants.environmentIntensity = std::max(environment->intensity, 0.0f);
            constants.prefilteredMaxLod = environment->prefilteredMaxLod;
            constants.environmentRotation[0] = std::cos(environment->rotation);
            constants.environmentRotation[1] = std::sin(environment->rotation);
        }
        return constants;
    }
}

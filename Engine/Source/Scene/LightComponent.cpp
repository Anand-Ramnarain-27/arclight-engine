#include "Scene/LightComponent.h"

#include "Scene/ComponentData.h"
#include "Scene/GameObject.h"
#include "Scene/Scene.h"

#include <algorithm>
#include <cmath>
#include <memory>
#include <string>

namespace arc
{
    namespace
    {
        // Rounded to a thousandth of a degree, so 35 degrees is saved as 35 rather than the
        // 35.000004 that converting to radians and back gives.
        float savedDegrees(float radians)
        {
            return std::round(toDegrees(radians) * 1000.0f) / 1000.0f;
        }
    }

    GpuLight LightComponent::toGpuLight(const Mat4& world) const
    {
        return makeGpuLight(light, world.translationPart(), world.transformVector(Vec3::forward()));
    }

    void LightComponent::save(ComponentData& data) const
    {
        data.setString("lightType", lightTypeName(light.type));
        data.setVec3("color", light.color);
        data.setFloat("intensity", light.intensity);
        if (light.type != LightType::Directional)
            data.setFloat("range", light.range);
        if (light.type == LightType::Spot)
        {
            data.setFloat("innerConeDegrees", savedDegrees(light.innerConeAngle));
            data.setFloat("outerConeDegrees", savedDegrees(light.outerConeAngle));
        }
    }

    void LightComponent::load(const ComponentData& data, const ComponentLoadContext& /*context*/)
    {
        if (const std::optional<std::string> type = data.getString("lightType"))
        {
            for (const LightType candidate : {LightType::Directional, LightType::Point, LightType::Spot})
                if (*type == lightTypeName(candidate))
                    light.type = candidate;
        }
        if (const std::optional<Vec3> color = data.getVec3("color"))
            light.color = {std::max(color->x, 0.0f), std::max(color->y, 0.0f), std::max(color->z, 0.0f)};
        if (const std::optional<float> intensity = data.getFloat("intensity"))
            light.intensity = std::max(*intensity, 0.0f);
        if (const std::optional<float> range = data.getFloat("range"))
            light.range = std::max(*range, 0.0f);
        if (const std::optional<float> outer = data.getFloat("outerConeDegrees"))
            light.outerConeAngle = std::clamp(toRadians(*outer), 0.0f, kHalfPi);
        if (const std::optional<float> inner = data.getFloat("innerConeDegrees"))
            light.innerConeAngle = std::clamp(toRadians(*inner), 0.0f, light.outerConeAngle);
    }

    LightDesc defaultLightDesc(LightType type)
    {
        LightDesc desc;
        desc.type = type;
        if (type == LightType::Directional)
        {
            desc.color = {1.0f, 0.96f, 0.9f};
            desc.intensity = 3.0f;
        }
        else
        {
            desc.intensity = 10.0f;
            desc.range = 10.0f;
            desc.innerConeAngle = toRadians(20.0f);
            desc.outerConeAngle = toRadians(30.0f);
        }
        return desc;
    }

    uint32_t collectLights(const Scene& scene, std::vector<GpuLight>& lights)
    {
        uint32_t count = 0;
        scene.forEachActive([&](const GameObject& object) {
            for (const std::unique_ptr<Component>& component : object.components())
            {
                if (!component->enabled || &component->type() != &LightComponent::staticType())
                    continue;
                lights.push_back(static_cast<const LightComponent&>(*component).toGpuLight(object.transform().worldMatrix()));
                ++count;
            }
        });
        return count;
    }

    GameObject& createLight(Scene& scene, LightType type, const Vec3& position)
    {
        GameObject& object = scene.create(std::string(lightTypeName(type)) + " Light");
        object.addComponent<LightComponent>(defaultLightDesc(type));
        if (type == LightType::Directional)
        {
            // Placed above the origin only so its gizmo is easy to find; a directional light's
            // position does not matter.
            object.transform().setLocalRotation(Quat::fromForward(defaultSunLight().direction));
            object.transform().setLocalPosition(Vec3{0.0f, 3.0f, 0.0f});
        }
        else
        {
            object.transform().setLocalRotation(Quat::fromForward({0.0f, -1.0f, 0.0f}));
            object.transform().setLocalPosition(position);
        }
        return object;
    }
}

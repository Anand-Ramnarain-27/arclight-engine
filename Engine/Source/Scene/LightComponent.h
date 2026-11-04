#pragma once

#include "Renderer/Lighting.h"
#include "Scene/Component.h"

#include <cstdint>
#include <vector>

namespace arc
{
    class GameObject;
    class Scene;

    /// A light at its GameObject's position, shining along the object's -Z (directional and spot
    /// lights), as glTF's KHR_lights_punctual defines lights on nodes. The object's scale has no effect.
    ///
    /// Saved with the light type by name (as "lightType": "type" is the serializer's own key) and the
    /// cone angles in degrees, so scene files read naturally.
    class LightComponent final : public Component
    {
        ARC_COMPONENT(LightComponent, "Light")

    public:
        LightComponent() = default;
        explicit LightComponent(const LightDesc& desc) : light(desc) {}

        /// The light as the shaders read it, placed by the object's world matrix.
        [[nodiscard]] GpuLight toGpuLight(const Mat4& world) const;

        void save(ComponentData& data) const override;
        void load(const ComponentData& data, const ComponentLoadContext& context) override;

        LightDesc light;
    };

    /// The lights a new light component of each type starts with: a 3 lux sun, and 10 cd point and
    /// spot lights reaching 10 m.
    [[nodiscard]] LightDesc defaultLightDesc(LightType type);

    /// Appends the enabled lights of every active object, in hierarchy order, and returns how many
    /// there were.
    uint32_t collectLights(const Scene& scene, std::vector<GpuLight>& lights);

    /// A new top-level object named after the type ("Directional Light", ...) with a default light.
    /// A directional light is turned to match defaultSunLight(); the others are placed at
    /// `position`, facing down.
    GameObject& createLight(Scene& scene, LightType type, const Vec3& position = Vec3::zero());
}

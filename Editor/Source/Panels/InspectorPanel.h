#pragma once

#include "Panels/Panel.h"
#include "Scene/GameObject.h"

#include <array>
#include <functional>
#include <memory>
#include <vector>

namespace arc
{
    class Component;
    class EditorSelection;
    class SceneModule;
    struct ComponentType;

    /// Shows and edits the selected object: its name and active flag, its transform, and each of
    /// its components. Components are added from the list of component editors and removed with
    /// their header's close button.
    ///
    /// Rotation is edited as Euler angles in degrees (yaw, pitch and roll as Quat::fromEuler applies
    /// them) but stored as a quaternion. The angles shown are kept from frame to frame instead of
    /// being recomputed from the quaternion, so dragging one never makes the others jump between
    /// equivalent values; they are recomputed only when the rotation changes from somewhere else.
    class InspectorPanel final : public Panel
    {
    public:
        InspectorPanel(SceneModule& scene, EditorSelection& selection);

        [[nodiscard]] const char* title() const override { return "Inspector"; }
        void draw(bool* open) override;

    private:
        /// How the inspector creates and draws one component type.
        struct ComponentEditor
        {
            const ComponentType* type = nullptr;
            std::function<std::unique_ptr<Component>()> create;
            std::function<void(Component&)> draw;
        };

        void drawHeader(GameObject& object);
        void drawTransform(GameObject& object);
        void drawComponents(GameObject& object);
        void drawAddComponent(GameObject& object);
        void drawMeshComponent(Component& component);
        void drawLightComponent(Component& component);
        [[nodiscard]] const ComponentEditor* editorFor(const ComponentType& type) const;

        SceneModule& m_scene;
        EditorSelection& m_selection;
        std::vector<ComponentEditor> m_editors;

        std::array<char, 256> m_nameBuffer{};
        GameObjectHandle m_nameFor;

        Vec3 m_eulerDegrees;
        Quat m_eulerSource;
        GameObjectHandle m_eulerFor;
    };
}

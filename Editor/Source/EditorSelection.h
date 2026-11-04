#pragma once

#include "Renderer/Lighting.h"
#include "Scene/GameObject.h"

#include <string>

namespace arc
{
    class SceneModule;

    /// The object the editor is working on, and the edits that act on it. Holds a handle, so the
    /// selection clears itself when the object is destroyed, by any means.
    class EditorSelection
    {
    public:
        explicit EditorSelection(SceneModule& scene) : m_scene(scene) {}

        /// Null when nothing is selected or the selected object is gone.
        [[nodiscard]] GameObject* object() const;
        [[nodiscard]] bool isSelected(const GameObject& object) const { return object.handle() == m_handle; }

        /// Also asks the Hierarchy panel to reveal the object: its ancestors are expanded and it is
        /// scrolled into view.
        void select(GameObject* object);
        void clear() { m_handle = {}; }

        /// Set by select(); the Hierarchy panel clears it once the object is shown.
        bool revealRequested = false;

        /// A new empty object at the end of `parent`'s children (or the top level), selected.
        GameObject& createEmpty(GameObject* parent, std::string name = "GameObject");
        /// A new top-level light, selected. See arc::createLight() for where it is placed.
        GameObject& createLight(LightType type, const Vec3& position);
        /// Duplicates the selected object and selects the copy.
        void duplicateSelected();
        /// Destroys the selected object and everything below it.
        void destroySelected();

    private:
        SceneModule& m_scene;
        GameObjectHandle m_handle;
    };
}

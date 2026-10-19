#include "EditorSelection.h"

#include "Scene/SceneModule.h"

namespace arc
{
    GameObject* EditorSelection::object() const
    {
        return m_scene.scene().find(m_handle);
    }

    void EditorSelection::select(GameObject* object)
    {
        m_handle = object ? object->handle() : GameObjectHandle{};
        revealRequested = object != nullptr;
    }

    GameObject& EditorSelection::createEmpty(GameObject* parent, std::string name)
    {
        GameObject& object = m_scene.scene().create(std::move(name), parent);
        select(&object);
        return object;
    }

    void EditorSelection::duplicateSelected()
    {
        if (GameObject* selected = object())
            select(&m_scene.scene().duplicate(*selected));
    }

    void EditorSelection::destroySelected()
    {
        if (GameObject* selected = object())
            m_scene.scene().destroy(*selected);
        clear();
    }
}

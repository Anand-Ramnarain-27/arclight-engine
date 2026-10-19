#include "Scene/GameObject.h"

#include "Core/Assert.h"
#include "Scene/Scene.h"

#include <algorithm>

namespace arc
{
    GameObject::GameObject(Scene& scene, GameObjectHandle handle, Uuid uuid, std::string name)
        : m_scene(scene)
        , m_handle(handle)
        , m_uuid(uuid)
        , m_name(std::move(name))
        , m_transform(*this)
    {
    }

    GameObject::~GameObject() = default;

    bool GameObject::isActiveInHierarchy() const
    {
        for (const GameObject* object = this; object; object = object->parent())
            if (!object->m_active)
                return false;
        return true;
    }

    GameObject* GameObject::parent() const
    {
        return m_parent == &m_scene.root() ? nullptr : m_parent;
    }

    bool GameObject::isDescendantOf(const GameObject& ancestor) const
    {
        for (const GameObject* object = m_parent; object; object = object->m_parent)
            if (object == &ancestor)
                return true;
        return false;
    }

    size_t GameObject::siblingIndex() const
    {
        ARC_ASSERT(m_parent != nullptr);
        const auto& siblings = m_parent->m_children;
        return static_cast<size_t>(std::find(siblings.begin(), siblings.end(), this) - siblings.begin());
    }

    bool GameObject::setParent(GameObject* newParent, bool keepWorldTransform, size_t index)
    {
        GameObject& target = newParent ? *newParent : m_scene.root();
        if (&target == this || &target.m_scene != &m_scene || target.isDescendantOf(*this))
            return false;

        // Taken before detaching: afterwards worldMatrix() would still be cached but no longer
        // describe where the object is.
        const Mat4 world = m_transform.worldMatrix();

        // Moving within the same parent: the index refers to the order with this object removed.
        if (&target == m_parent && index != kAppend && index > siblingIndex())
            --index;

        detachFromParent();
        attachTo(target, index);

        // If the world matrix cannot be kept (a parent scaled to zero), the local values are kept.
        if (keepWorldTransform)
            m_transform.setWorldMatrix(world);
        m_transform.markWorldDirty();
        return true;
    }

    void GameObject::detachFromParent()
    {
        if (!m_parent)
            return;
        auto& siblings = m_parent->m_children;
        siblings.erase(std::find(siblings.begin(), siblings.end(), this));
        m_parent = nullptr;
    }

    void GameObject::attachTo(GameObject& parent, size_t index)
    {
        ARC_ASSERT(m_parent == nullptr);
        auto& siblings = parent.m_children;
        siblings.insert(siblings.begin() + static_cast<std::ptrdiff_t>(std::min(index, siblings.size())), this);
        m_parent = &parent;
    }

    Component& GameObject::addComponent(std::unique_ptr<Component> component)
    {
        ARC_ASSERT(component && component->m_owner == nullptr);
        component->m_owner = this;
        m_components.push_back(std::move(component));
        return *m_components.back();
    }

    bool GameObject::removeComponent(const Component& component)
    {
        const auto it = std::find_if(m_components.begin(), m_components.end(),
                                     [&component](const std::unique_ptr<Component>& c) { return c.get() == &component; });
        if (it == m_components.end())
            return false;
        m_components.erase(it);
        return true;
    }
}

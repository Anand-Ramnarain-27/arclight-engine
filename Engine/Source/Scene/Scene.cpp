#include "Scene/Scene.h"

#include "Core/Assert.h"

namespace arc
{
    Scene::Scene()
    {
        m_root = &allocate("Root", Uuid{});
    }

    // Children are removed before their parents so no object outlives the one that lists it.
    Scene::~Scene()
    {
        while (!m_root->children().empty())
            release(*m_root->children().back());
    }

    GameObject& Scene::allocate(std::string name, Uuid uuid)
    {
        const GameObjectHandle handle = m_objects.insert(nullptr);
        ARC_ASSERT(handle, "The scene's object pool is full");
        // The constructor is private to GameObject, so std::make_unique cannot reach it.
        std::unique_ptr<GameObject>& slot = *m_objects.get(handle);
        slot.reset(new GameObject(*this, handle, uuid, std::move(name)));
        return *slot;
    }

    GameObject& Scene::create(std::string name, GameObject* parent)
    {
        GameObject& object = allocate(std::move(name), Uuid::generate());
        object.attachTo(parent ? *parent : *m_root, GameObject::kAppend);
        return object;
    }

    GameObject& Scene::create(std::string name, GameObject* parent, Uuid uuid)
    {
        ARC_ASSERT(!uuid.isNull());
        GameObject& object = allocate(std::move(name), uuid);
        object.attachTo(parent ? *parent : *m_root, GameObject::kAppend);
        return object;
    }

    void Scene::clear()
    {
        while (!m_root->children().empty())
            release(*m_root->children().back());
        m_pendingDestroy.clear();
    }

    GameObject& Scene::cloneSubtree(const GameObject& original, GameObject& parent, size_t index)
    {
        GameObject& copy = allocate(original.name(), Uuid::generate());
        copy.attachTo(parent, index);
        copy.m_active = original.m_active;
        copy.m_transform.copyLocalFrom(original.m_transform);
        for (const std::unique_ptr<Component>& component : original.components())
            copy.addComponent(component->clone());
        for (const GameObject* child : original.children())
            if (!child->m_pendingDestroy)
                cloneSubtree(*child, copy, GameObject::kAppend);
        return copy;
    }

    GameObject& Scene::duplicate(const GameObject& original)
    {
        ARC_ASSERT(&original.scene() == this && &original != m_root);
        return cloneSubtree(original, *original.m_parent, original.siblingIndex() + 1);
    }

    void Scene::destroy(GameObject& object)
    {
        ARC_ASSERT(&object.scene() == this);
        if (&object == m_root || object.m_pendingDestroy)
            return;
        markForDestruction(object);
        m_pendingDestroy.push_back(object.handle());
    }

    void Scene::markForDestruction(GameObject& object)
    {
        object.m_pendingDestroy = true;
        for (GameObject* child : object.children())
            markForDestruction(*child);
    }

    void Scene::flushDestroyed()
    {
        // A handle is stale here if an ancestor queued earlier in the list already released it.
        for (const GameObjectHandle handle : m_pendingDestroy)
            if (const std::unique_ptr<GameObject>* slot = m_objects.get(handle))
                release(**slot);
        m_pendingDestroy.clear();
    }

    void Scene::release(GameObject& object)
    {
        while (!object.children().empty())
            release(*object.children().back());
        object.detachFromParent();
        // Removing the slot destroys the object; the handle is copied first because it lives inside it.
        const GameObjectHandle handle = object.handle();
        m_objects.remove(handle);
    }

    GameObject* Scene::find(GameObjectHandle handle) const
    {
        const std::unique_ptr<GameObject>* slot = m_objects.get(handle);
        if (!slot || slot->get() == m_root || (*slot)->m_pendingDestroy)
            return nullptr;
        return slot->get();
    }

    GameObject* Scene::findByUuid(Uuid uuid) const
    {
        GameObject* found = nullptr;
        m_objects.forEach([&](GameObjectHandle, const std::unique_ptr<GameObject>& object) {
            if (!found && object && object.get() != m_root && !object->m_pendingDestroy && object->uuid() == uuid)
                found = object.get();
        });
        return found;
    }

    void Scene::update(const FrameTime& time)
    {
        forEachActive([&time](GameObject& object) {
            for (const std::unique_ptr<Component>& component : object.components())
                if (component->enabled)
                    component->update(time);
        });
    }
}

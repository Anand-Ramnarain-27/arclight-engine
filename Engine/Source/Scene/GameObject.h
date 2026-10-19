#pragma once

#include "Core/Handle.h"
#include "Scene/Component.h"
#include "Scene/Transform.h"
#include "Scene/Uuid.h"

#include <cstddef>
#include <limits>
#include <memory>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace arc
{
    class GameObject;
    class Scene;

    /// Refers to a GameObject while the program runs. Becomes stale, and Scene::find() returns
    /// nullptr, once the object is destroyed, so it is safe to keep across frames (a selection, a
    /// target). The Uuid is the identity that survives saving and loading.
    using GameObjectHandle = Handle<GameObject>;

    /// An object in a Scene: a name, a Transform, a place in the hierarchy and a list of components.
    ///
    /// Created and destroyed only through its Scene, which owns it. Children are ordered; the order
    /// is the one the Hierarchy panel shows and scene files store.
    class GameObject
    {
    public:
        static constexpr size_t kAppend = std::numeric_limits<size_t>::max();

        GameObject(const GameObject&) = delete;
        GameObject& operator=(const GameObject&) = delete;
        ~GameObject();

        [[nodiscard]] Scene& scene() const { return m_scene; }
        [[nodiscard]] GameObjectHandle handle() const { return m_handle; }
        [[nodiscard]] Uuid uuid() const { return m_uuid; }

        [[nodiscard]] const std::string& name() const { return m_name; }
        void setName(std::string name) { m_name = std::move(name); }

        /// This object's own flag. An inactive object, and everything below it, is neither updated
        /// nor drawn.
        [[nodiscard]] bool isActiveSelf() const { return m_active; }
        void setActive(bool active) { m_active = active; }
        /// Active itself and every ancestor active.
        [[nodiscard]] bool isActiveInHierarchy() const;

        [[nodiscard]] Transform& transform() { return m_transform; }
        [[nodiscard]] const Transform& transform() const { return m_transform; }

        /// Null for a top-level object.
        [[nodiscard]] GameObject* parent() const;
        [[nodiscard]] std::span<GameObject* const> children() const { return m_children; }

        /// Moves the object under `newParent` (null for the top level), at `index` among its new
        /// siblings (clamped; kAppend for the end). With `keepWorldTransform` the local transform
        /// is recomputed so the object stays where it is on screen; otherwise the local values are
        /// kept and it moves with its new parent. Returns false, and changes nothing, if
        /// `newParent` is this object or one of its descendants, or belongs to another scene.
        bool setParent(GameObject* newParent, bool keepWorldTransform = true, size_t index = kAppend);

        /// True if `ancestor` is this object's parent, grandparent and so on. Not true for itself.
        [[nodiscard]] bool isDescendantOf(const GameObject& ancestor) const;

        /// Position among the parent's children (or the top-level objects).
        [[nodiscard]] size_t siblingIndex() const;

        /// Takes ownership of `component`. Several components of the same type are allowed.
        Component& addComponent(std::unique_ptr<Component> component);

        template <ComponentClass T, typename... Args>
        T& addComponent(Args&&... args)
        {
            return static_cast<T&>(addComponent(std::make_unique<T>(std::forward<Args>(args)...)));
        }

        /// The first component of type T, or nullptr.
        template <ComponentClass T>
        [[nodiscard]] T* getComponent() const
        {
            for (const std::unique_ptr<Component>& component : m_components)
                if (&component->type() == &T::staticType())
                    return static_cast<T*>(component.get());
            return nullptr;
        }

        /// Destroys the component immediately. Returns false if it is not attached to this object.
        bool removeComponent(const Component& component);

        [[nodiscard]] std::span<const std::unique_ptr<Component>> components() const { return m_components; }

    private:
        friend class Scene;
        GameObject(Scene& scene, GameObjectHandle handle, Uuid uuid, std::string name);

        void detachFromParent();
        void attachTo(GameObject& parent, size_t index);

        Scene& m_scene;
        GameObjectHandle m_handle;
        Uuid m_uuid;
        std::string m_name;
        bool m_active = true;
        bool m_pendingDestroy = false;
        Transform m_transform;
        // Never null, except for the scene's hidden root: top-level objects are the root's children.
        GameObject* m_parent = nullptr;
        std::vector<GameObject*> m_children;
        std::vector<std::unique_ptr<Component>> m_components;
    };
}

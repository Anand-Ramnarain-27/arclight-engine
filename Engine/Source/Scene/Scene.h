#pragma once

#include "Core/Handle.h"
#include "Core/Time.h"
#include "Scene/GameObject.h"

#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <vector>

namespace arc
{
    /// Owns GameObjects and their hierarchy.
    ///
    /// Objects live at stable addresses until destroyed, so pointers to them stay valid within a
    /// frame; across frames, keep a GameObjectHandle and look it up with find(). destroy() only
    /// marks an object; it is removed by flushDestroyed(), which the owner calls once per frame
    /// where nothing is iterating the hierarchy (SceneModule does it in preRender()). Not
    /// thread-safe.
    class Scene
    {
    public:
        Scene();
        ~Scene();

        Scene(const Scene&) = delete;
        Scene& operator=(const Scene&) = delete;

        /// A new object with an identity transform, at the end of `parent`'s children, or of the
        /// top-level objects if `parent` is null.
        GameObject& create(std::string name, GameObject* parent = nullptr);
        /// The same, with a given UUID, for loading saved objects. `uuid` must not be null, and the
        /// caller makes sure it is unique.
        GameObject& create(std::string name, GameObject* parent, Uuid uuid);

        /// Destroys every object at once, immediately rather than at flushDestroyed(). Handles to
        /// them become stale. Must not be called while the hierarchy is being walked.
        void clear();

        /// A deep copy of `original` and everything below it, with new UUIDs and copies of every
        /// component, placed right after the original among its siblings.
        GameObject& duplicate(const GameObject& original);

        /// Marks the object and its descendants for removal at the next flushDestroyed(). They stay
        /// valid until then, and are skipped by update() and forEachActive().
        void destroy(GameObject& object);
        void flushDestroyed();

        /// Null if the handle is null, stale, or refers to an object marked for destruction.
        [[nodiscard]] GameObject* find(GameObjectHandle handle) const;

        /// Linear search. Null if there is none.
        [[nodiscard]] GameObject* findByUuid(Uuid uuid) const;

        /// Top-level objects, in order.
        [[nodiscard]] std::span<GameObject* const> roots() const { return m_root->children(); }

        /// Every object, including those inside inactive parents. The hidden root is not counted.
        [[nodiscard]] uint32_t objectCount() const { return m_objects.size() - 1; }

        /// Updates the enabled components of every active object, parents before children. Components
        /// may destroy objects (that is deferred) but must not create or reparent them here.
        void update(const FrameTime& time);

        /// Calls `function(GameObject&)` for every active object not marked for destruction, depth
        /// first, parents before children. Skips the subtrees of inactive objects. The function must
        /// not add or remove objects, or reparent them.
        template <typename Function>
        void forEachActive(Function&& function) const
        {
            for (GameObject* object : roots())
                visitActive(*object, function);
        }

        /// The parent of all top-level objects. It is never destroyed, shown or saved.
        [[nodiscard]] GameObject& root() const { return *m_root; }

    private:
        GameObject& allocate(std::string name, Uuid uuid);
        GameObject& cloneSubtree(const GameObject& original, GameObject& parent, size_t index);
        void markForDestruction(GameObject& object);
        void release(GameObject& object);

        template <typename Function>
        static void visitActive(GameObject& object, Function& function)
        {
            if (!object.isActiveSelf() || object.m_pendingDestroy)
                return;
            function(object);
            for (GameObject* child : object.children())
                visitActive(*child, function);
        }

        HandlePool<std::unique_ptr<GameObject>, GameObject> m_objects;
        GameObject* m_root = nullptr;
        std::vector<GameObjectHandle> m_pendingDestroy;
    };
}

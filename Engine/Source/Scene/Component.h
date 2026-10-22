#pragma once

#include "Core/Time.h"

#include <concepts>
#include <memory>
#include <string_view>

namespace arc
{
    class ComponentData;
    class GameObject;
    struct ComponentLoadContext;

    /// Identifies a component class. There is exactly one per class, so types compare by address,
    /// which needs no RTTI.
    struct ComponentType
    {
        /// Shown in the editor, and stored in scene files, so it must never change once used.
        std::string_view name;
    };

    /// Something attached to a GameObject that gives it data or behaviour. Owned by its GameObject.
    ///
    /// Every component class declares ARC_COMPONENT(Class, "Name") in its body, which supplies type()
    /// and clone(). clone() copy-constructs, so a component that must not be copied member by member
    /// writes its own copy constructor.
    class Component
    {
    public:
        virtual ~Component() = default;

        Component& operator=(const Component&) = delete;

        [[nodiscard]] virtual const ComponentType& type() const = 0;

        /// A copy that is not attached to any GameObject yet.
        [[nodiscard]] virtual std::unique_ptr<Component> clone() const = 0;

        /// Called once per frame for enabled components of active GameObjects. Uses the game clock's
        /// delta for simulation, which is zero while the game is paused or stopped.
        virtual void update(const FrameTime& /*time*/) {}

        /// Writes the component's fields for a scene or prefab file. `enabled` and the type are
        /// written by the serializer. A component with nothing to save keeps the default.
        virtual void save(ComponentData& /*data*/) const {}

        /// Reads what save() wrote. Fields missing from `data` keep their current values, so files
        /// written before a field existed still load.
        virtual void load(const ComponentData& /*data*/, const ComponentLoadContext& /*context*/) {}

        /// Valid once the component has been added to a GameObject.
        [[nodiscard]] GameObject& owner() const { return *m_owner; }

        bool enabled = true;

    protected:
        Component() = default;
        // The copy is detached: GameObject::addComponent() sets the owner.
        Component(const Component& other) : enabled(other.enabled) {}

    private:
        friend class GameObject;
        GameObject* m_owner = nullptr;
    };

    template <typename T>
    concept ComponentClass = std::derived_from<T, Component> && requires {
        { T::staticType() } -> std::same_as<const ComponentType&>;
    };
}

#define ARC_COMPONENT(Class, DisplayName)                                                         \
public:                                                                                           \
    [[nodiscard]] static const ::arc::ComponentType& staticType()                                 \
    {                                                                                             \
        static const ::arc::ComponentType instance{DisplayName};                                  \
        return instance;                                                                          \
    }                                                                                             \
    [[nodiscard]] const ::arc::ComponentType& type() const override { return staticType(); }      \
    [[nodiscard]] std::unique_ptr<::arc::Component> clone() const override                        \
    {                                                                                             \
        return std::make_unique<Class>(*this);                                                    \
    }

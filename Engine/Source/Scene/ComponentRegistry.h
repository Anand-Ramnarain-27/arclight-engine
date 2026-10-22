#pragma once

#include "Scene/Component.h"
#include "Scene/ComponentData.h"

#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace arc
{
    /// Creates components from the type names stored in scene files.
    class ComponentRegistry
    {
    public:
        using Factory = std::function<std::unique_ptr<Component>()>;

        template <ComponentClass T>
        void add()
        {
            add(T::staticType(), [] { return std::make_unique<T>(); });
        }
        void add(const ComponentType& type, Factory factory);

        /// Null for a name that is not registered.
        [[nodiscard]] std::unique_ptr<Component> create(std::string_view name) const;
        [[nodiscard]] bool contains(std::string_view name) const;

        /// Every component the engine defines.
        [[nodiscard]] static const ComponentRegistry& engine();

    private:
        struct Entry
        {
            const ComponentType* type;
            Factory factory;
        };
        std::vector<Entry> m_entries;
    };

    /// Stands in for a component whose type is not registered, for example one from a newer
    /// version or a module that is not loaded. It keeps the saved fields as they were and writes
    /// them back unchanged, so loading and saving a scene never loses a component.
    class MissingComponent final : public Component
    {
        ARC_COMPONENT(MissingComponent, "Missing")

    public:
        MissingComponent(std::string typeName, ComponentData data)
            : m_typeName(std::move(typeName))
            , m_data(std::move(data))
        {
        }

        /// The type name in the file.
        [[nodiscard]] const std::string& typeName() const { return m_typeName; }
        [[nodiscard]] const ComponentData& data() const { return m_data; }

    private:
        std::string m_typeName;
        ComponentData m_data;
    };
}

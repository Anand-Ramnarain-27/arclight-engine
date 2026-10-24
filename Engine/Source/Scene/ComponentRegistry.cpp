#include "Scene/ComponentRegistry.h"

#include "Scene/LightComponent.h"
#include "Scene/MeshComponent.h"

#include <algorithm>

namespace arc
{
    void ComponentRegistry::add(const ComponentType& type, Factory factory)
    {
        std::erase_if(m_entries, [&](const Entry& entry) { return entry.type->name == type.name; });
        m_entries.push_back({&type, std::move(factory)});
    }

    std::unique_ptr<Component> ComponentRegistry::create(std::string_view name) const
    {
        for (const Entry& entry : m_entries)
            if (entry.type->name == name)
                return entry.factory();
        return nullptr;
    }

    bool ComponentRegistry::contains(std::string_view name) const
    {
        return std::any_of(m_entries.begin(), m_entries.end(), [name](const Entry& entry) { return entry.type->name == name; });
    }

    const ComponentRegistry& ComponentRegistry::engine()
    {
        static const ComponentRegistry registry = [] {
            ComponentRegistry result;
            result.add<MeshComponent>();
            result.add<LightComponent>();
            return result;
        }();
        return registry;
    }
}

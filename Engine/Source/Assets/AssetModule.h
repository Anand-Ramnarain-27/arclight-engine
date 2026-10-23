#pragma once

#include "Application/Module.h"
#include "Assets/AssetDatabase.h"
#include "Assets/ResourceManager.h"

#include <memory>

namespace arc
{
    /// Owns the asset database and the resource manager. Scans assets:// in init(), so it must be
    /// added before the modules that load assets, and is shut down after them.
    class AssetModule final : public Module
    {
    public:
        explicit AssetModule(Application& app);
        ~AssetModule() override;

        [[nodiscard]] std::string_view name() const override { return "Assets"; }

        [[nodiscard]] bool init() override;
        void shutdown() override;

        [[nodiscard]] AssetDatabase& database() { return *m_database; }
        [[nodiscard]] ResourceManager& resources() { return *m_resources; }

    private:
        std::unique_ptr<AssetDatabase> m_database;
        std::unique_ptr<ResourceManager> m_resources;
    };
}

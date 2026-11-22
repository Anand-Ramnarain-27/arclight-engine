#pragma once

#include "Application/Module.h"
#include "Assets/AssetDatabase.h"
#include "Assets/ResourceManager.h"

#include <memory>

namespace arc
{
    /// Owns the asset database and the resource manager. Scans assets:// in init(), so it must be
    /// added before the modules that load assets, and is shut down after them. Its update() uploads
    /// the models loaded in the background, so they are ready before the later modules' update().
    class AssetModule final : public Module
    {
    public:
        explicit AssetModule(Application& app);
        ~AssetModule() override;

        [[nodiscard]] std::string_view name() const override { return "Assets"; }

        [[nodiscard]] bool init() override;
        void update(const FrameTime& time) override;
        void shutdown() override;

        /// Time each frame may spend uploading background loads. A model that takes longer is still
        /// uploaded whole; only the next one waits for another frame.
        double uploadBudgetMilliseconds = 4.0;

        [[nodiscard]] AssetDatabase& database() { return *m_database; }
        [[nodiscard]] ResourceManager& resources() { return *m_resources; }

    private:
        std::unique_ptr<AssetDatabase> m_database;
        std::unique_ptr<ResourceManager> m_resources;
    };
}

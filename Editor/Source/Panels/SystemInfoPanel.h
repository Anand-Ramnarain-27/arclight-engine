#pragma once

#include "Panels/Panel.h"
#include "Platform/SystemInfo.h"

#include <optional>

namespace arc
{
    class Application;

    /// Hardware, OS, display and build information.
    class SystemInfoPanel final : public Panel
    {
    public:
        explicit SystemInfoPanel(Application& app) : m_app(app) {}

        [[nodiscard]] const char* title() const override { return "System Info"; }
        void draw(bool* open) override;

    private:
        Application& m_app;

        // Queried on first draw rather than at construction, because the window must exist.
        std::optional<SystemInfo> m_info;
    };
}

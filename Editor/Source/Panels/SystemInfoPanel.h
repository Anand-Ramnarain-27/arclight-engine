#pragma once

#include "Panels/Panel.h"
#include "Platform/SystemInfo.h"

#include <optional>

namespace arc
{
    class Application;
    class ImGuiModule;

    /// Hardware, OS, display and build information.
    class SystemInfoPanel final : public Panel
    {
    public:
        SystemInfoPanel(Application& app, ImGuiModule& imgui) : m_app(app), m_imgui(imgui) {}

        [[nodiscard]] const char* title() const override { return "System Info"; }
        void draw(bool* open) override;

    private:
        Application& m_app;
        ImGuiModule& m_imgui;

        // Queried on first draw rather than at construction, because the window must exist.
        std::optional<SystemInfo> m_info;
    };
}

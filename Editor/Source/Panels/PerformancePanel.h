#pragma once

#include "Core/Time.h"
#include "Panels/Panel.h"

#include <array>

namespace arc
{
    class Application;
    class ImGuiModule;

    /// Frame time and FPS graphs, vsync and the frame-rate limiter.
    class PerformancePanel final : public Panel
    {
    public:
        PerformancePanel(Application& app, ImGuiModule& imgui) : m_app(app), m_imgui(imgui) {}

        [[nodiscard]] const char* title() const override { return "Performance"; }
        void draw(bool* open) override;

    private:
        Application& m_app;
        ImGuiModule& m_imgui;
        std::array<float, FrameStats::kHistorySize> m_fpsHistory{};
        int m_limitTarget = 60;
    };
}

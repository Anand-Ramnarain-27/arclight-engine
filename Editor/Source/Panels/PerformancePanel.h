#pragma once

#include "Core/Time.h"
#include "Panels/Panel.h"

#include <array>

namespace arc
{
    class Application;

    /// Frame time and FPS graphs, vsync and the frame-rate limiter.
    class PerformancePanel final : public Panel
    {
    public:
        explicit PerformancePanel(Application& app) : m_app(app) {}

        [[nodiscard]] const char* title() const override { return "Performance"; }
        void draw(bool* open) override;

    private:
        Application& m_app;
        std::array<float, FrameStats::kHistorySize> m_fpsHistory{};
        int m_limitTarget = 60;
    };
}

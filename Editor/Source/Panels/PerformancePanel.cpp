#include "Panels/PerformancePanel.h"

#include "Application/Application.h"
#include "UI/ImGuiModule.h"

#include <imgui.h>

#include <algorithm>
#include <format>

namespace arc
{
    void PerformancePanel::draw(bool* open)
    {
        if (!ImGui::Begin(title(), open))
        {
            ImGui::End();
            return;
        }

        const FrameStats& stats = m_app.frameStats();
        const FrameTime& time = m_app.frameTime();
        const auto& history = stats.history();
        const int offset = static_cast<int>(stats.offset());
        const int count = static_cast<int>(history.size());

        ImGui::Text("%.1f FPS  (%.2f ms)", stats.averageFps(), stats.averageMs());
        ImGui::TextDisabled("Frame %llu   Real time %.1f s", static_cast<unsigned long long>(time.frameIndex), time.realTime);

        // A fixed lower bound of 33.3 ms (30 FPS) keeps the scale stable instead of rescaling every frame.
        const float maxMs = std::max(33.3f, *std::max_element(history.begin(), history.end()) * 1.1f);
        const std::string msOverlay = std::format("frame time (ms), avg {:.2f}", stats.averageMs());
        ImGui::PlotLines("##frameMs", history.data(), count, offset, msOverlay.c_str(), 0.0f, maxMs,
                         ImVec2(-1.0f, ImGui::GetFontSize() * 5.0f));

        float maxFps = 0.0f;
        for (size_t i = 0; i < history.size(); ++i)
        {
            m_fpsHistory[i] = history[i] > 0.0f ? 1000.0f / history[i] : 0.0f;
            maxFps = std::max(maxFps, m_fpsHistory[i]);
        }
        const std::string fpsOverlay = std::format("FPS, avg {:.1f}", stats.averageFps());
        ImGui::PlotHistogram("##fps", m_fpsHistory.data(), count, offset, fpsOverlay.c_str(), 0.0f, maxFps * 1.1f,
                             ImVec2(-1.0f, ImGui::GetFontSize() * 5.0f));

        ImGui::SeparatorText("Frame pacing");

        bool vsync = m_imgui.vsync();
        if (ImGui::Checkbox("VSync", &vsync))
            m_imgui.setVSync(vsync);

        bool limit = m_app.maxFrameRate() != 0;
        if (ImGui::Checkbox("Limit frame rate", &limit))
            m_app.setMaxFrameRate(limit ? static_cast<uint32_t>(m_limitTarget) : 0);

        ImGui::BeginDisabled(!limit);
        if (ImGui::SliderInt("Max FPS", &m_limitTarget, 15, 240) && limit)
            m_app.setMaxFrameRate(static_cast<uint32_t>(m_limitTarget));
        ImGui::EndDisabled();

        if (!vsync && !limit)
            ImGui::TextColored(ImVec4(0.98f, 0.78f, 0.30f, 1.0f), "Uncapped: the loop runs as fast as the CPU allows.");

        ImGui::End();
    }
}

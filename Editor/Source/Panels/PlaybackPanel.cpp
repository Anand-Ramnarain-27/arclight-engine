#include "Panels/PlaybackPanel.h"

#include "Application/Application.h"

#include <imgui.h>

namespace arc
{
    void PlaybackPanel::draw(bool* open)
    {
        if (!ImGui::Begin(title(), open))
        {
            ImGui::End();
            return;
        }

        GameClock& clock = m_app.gameClock();
        const bool playing = clock.state() == GameClock::State::Playing;

        if (ImGui::Button(playing ? "Pause" : "Play"))
        {
            if (playing)
                clock.pause();
            else
                clock.play();
        }
        ImGui::SetItemTooltip("F5");
        ImGui::SameLine();

        ImGui::BeginDisabled(clock.state() == GameClock::State::Stopped);
        if (ImGui::Button("Stop"))
            clock.stop();
        ImGui::SetItemTooltip("Shift+F5");
        ImGui::EndDisabled();
        ImGui::SameLine();

        ImGui::BeginDisabled(playing);
        if (ImGui::Button("Step"))
            clock.step();
        ImGui::SetItemTooltip("F6: advance one frame, then pause");
        ImGui::EndDisabled();

        ImGui::SameLine();
        ImGui::TextDisabled("%s   game %.2f s", GameClock::stateName(clock.state()), m_app.frameTime().gameTime);

        float scale = clock.timeScale();
        // Logarithmic so 0.1x and 0.25x are as easy to hit as 4x and 8x.
        if (ImGui::SliderFloat("Time scale", &scale, GameClock::kMinTimeScale, GameClock::kMaxTimeScale, "%.2fx",
                               ImGuiSliderFlags_Logarithmic))
            clock.setTimeScale(scale);
        ImGui::SameLine();
        if (ImGui::Button("1x"))
            clock.setTimeScale(1.0f);

        ImGui::End();
    }
}

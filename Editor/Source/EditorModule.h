#pragma once

#include "Application/Module.h"

namespace arc
{
    /// Editor controls. Until the ImGui shell exists, state is shown in the window title.
    ///
    ///   F5          Play / Pause
    ///   Shift+F5    Stop
    ///   F6          Step one frame
    ///   + / -       Double / halve the time scale
    ///   0           Reset the time scale to 1
    class EditorModule final : public Module
    {
    public:
        using Module::Module;

        [[nodiscard]] std::string_view name() const override { return "Editor"; }

        void update(const FrameTime& time) override;

    private:
        void handlePlayControls();
        void logInputActivity();
        void refreshTitle(const FrameTime& time);

        static constexpr double kTitleRefreshSeconds = 0.25;
        double m_nextTitleRefresh = 0.0;
    };
}

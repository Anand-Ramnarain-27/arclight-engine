#pragma once

#include "Panels/Panel.h"

namespace arc
{
    class Application;

    /// Play / Pause / Stop / Step and the game time scale.
    class PlaybackPanel final : public Panel
    {
    public:
        explicit PlaybackPanel(Application& app) : m_app(app) {}

        [[nodiscard]] const char* title() const override { return "Playback"; }
        void draw(bool* open) override;

    private:
        Application& m_app;
    };
}

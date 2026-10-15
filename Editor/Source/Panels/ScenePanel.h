#pragma once

#include "Panels/Panel.h"

namespace arc
{
    class Application;
    class ImGuiModule;

    /// Shows the renderer's scene target. The target is resized to the panel's size in pixels, so
    /// the scene always renders at the resolution it is displayed at.
    class ScenePanel final : public Panel
    {
    public:
        ScenePanel(Application& app, ImGuiModule& imgui) : m_app(app), m_imgui(imgui) {}

        [[nodiscard]] const char* title() const override { return "Scene"; }
        void draw(bool* open) override;

        /// True while the mouse is over the scene image. Camera controls (2.5) only respond then.
        [[nodiscard]] bool isHovered() const { return m_hovered; }

    private:
        Application& m_app;
        ImGuiModule& m_imgui;
        bool m_hovered = false;
    };
}

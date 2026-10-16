#pragma once

#include "Panels/Panel.h"

struct ImVec2;

namespace arc
{
    class Application;
    class EditorCamera;
    class EditorGrid;
    class ImGuiModule;

    /// Shows the renderer's scene target and drives the editor camera. The target is resized to the
    /// panel's size in pixels, so the scene always renders at the resolution it is displayed at.
    ///
    /// The panel owns the mouse and keyboard while the pointer is over the image, or while a drag
    /// that started there is held: Input stops reporting them as captured by the UI, and the camera
    /// reads them. Overlays: resolution and API (top left), camera and grid settings (top left,
    /// below), and an axis gizmo (top right) whose axis ends turn the view to look along them.
    class ScenePanel final : public Panel
    {
    public:
        ScenePanel(Application& app, ImGuiModule& imgui, EditorCamera& camera, EditorGrid& grid)
            : m_app(app)
            , m_imgui(imgui)
            , m_camera(camera)
            , m_grid(grid)
        {
        }

        [[nodiscard]] const char* title() const override { return "Scene"; }
        void draw(bool* open) override;

    private:
        void drawSettings();
        void drawAxisGizmo(ImVec2 viewMin, ImVec2 viewMax);

        Application& m_app;
        ImGuiModule& m_imgui;
        EditorCamera& m_camera;
        EditorGrid& m_grid;
    };
}

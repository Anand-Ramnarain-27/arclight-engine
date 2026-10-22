#pragma once

#include "Panels/Panel.h"
#include "Scene/TransformGizmo.h"

#include <array>
#include <functional>
#include <string>

struct ImVec2;

namespace arc
{
    class Application;
    class EditorCamera;
    class EditorGrid;
    class EditorSelection;
    class ImGuiModule;
    class SceneModule;
    class SelectionOutline;

    /// Shows the renderer's scene target and drives the editor camera. The target is resized to the
    /// panel's size in pixels, so the scene always renders at the resolution it is displayed at.
    ///
    /// The panel owns the mouse and keyboard while the pointer is over the image, or while a drag
    /// that started there is held: Input stops reporting them as captured by the UI, and the camera
    /// reads them. A left click selects the object under the pointer (or clears the selection over
    /// empty space), the transform gizmo moves the selection, and F frames it. While the pointer is
    /// over a gizmo handle, or a handle is dragged, the mouse goes to the gizmo instead.
    ///
    /// Overlays: resolution and API (top left); the View menu (camera, grid, shading, gizmo and
    /// outline settings) and the gizmo toolbar (below); an axis gizmo (top right) whose axis ends
    /// turn the view to look along them.
    class ScenePanel final : public Panel
    {
    public:
        ScenePanel(Application& app, ImGuiModule& imgui, SceneModule& scene, EditorSelection& selection,
                   EditorCamera& camera, EditorGrid& grid, SelectionOutline& outline)
            : m_app(app)
            , m_imgui(imgui)
            , m_scene(scene)
            , m_selection(selection)
            , m_camera(camera)
            , m_grid(grid)
            , m_outline(outline)
        {
        }

        [[nodiscard]] const char* title() const override { return "Scene"; }
        void draw(bool* open) override;

        /// Called with the path of a model dragged from the Files panel and dropped on the view.
        std::function<void(const std::string& path)> onModelDropped;

    private:
        void browseForSkybox();
        void setSkyboxFolder(const std::string& folder);
        void drawSettings();
        void drawGizmoToolbar();
        void handleGizmoShortcuts();
        /// Selects what is under `mouse` (screen coordinates, in points), or clears the selection.
        void pick(ImVec2 mouse, ImVec2 viewMin, ImVec2 viewSize);
        void drawAxisGizmo(ImVec2 viewMin, ImVec2 viewMax);

        Application& m_app;
        ImGuiModule& m_imgui;
        SceneModule& m_scene;
        EditorSelection& m_selection;
        EditorCamera& m_camera;
        EditorGrid& m_grid;
        SelectionOutline& m_outline;
        TransformGizmo m_gizmo;
        std::array<char, 512> m_skyboxFolder{};
        /// Applied at the start of the next draw(), which runs outside the frame: creating the cube
        /// texture uploads and waits for the GPU.
        std::string m_pendingSkyboxFolder;
        bool m_pendingProceduralSky = false;
    };
}

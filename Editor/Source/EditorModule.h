#pragma once

#include "Application/Module.h"
#include "Panels/Panel.h"
#include "Scene/EditorCamera.h"
#include "Scene/EditorGrid.h"

#include <memory>
#include <vector>

namespace arc
{
    class ImGuiModule;

    /// The editor: main menu, dockable panels, global shortcuts, and the Scene view's camera and grid.
    ///
    /// Must be added before any module with UI: the dock space has to exist before the windows it
    /// hosts are submitted. The scene camera is handed to the Renderer in update(), so it applies to
    /// the whole frame's rendering. The grid is drawn by EditorOverlayModule, which comes after the
    /// modules that draw the scene. Camera controls are listed in EditorCamera.h.
    ///
    ///   F5          Play / Pause
    ///   Shift+F5    Stop
    ///   F6          Step one frame
    ///   + / -       Double / halve the time scale
    ///   0           Reset the time scale to 1
    ///   F11         Capture a GPU frame (RenderDoc, PIX or Xcode, when attached)
    class EditorModule final : public Module
    {
    public:
        /// `imgui` gives panels ImTextureIDs for engine textures, such as the scene target.
        EditorModule(Application& app, ImGuiModule& imgui);
        ~EditorModule() override;

        [[nodiscard]] std::string_view name() const override { return "Editor"; }

        [[nodiscard]] bool init() override;
        void update(const FrameTime& time) override;
        void shutdown() override;

        /// Draws over the scene: the grid. Called by EditorOverlayModule.
        void renderSceneOverlays();

    private:
        void handleShortcuts();
        void requestGpuCapture();
        void logInputActivity();
        void drawDockSpace();
        void buildDefaultLayout(unsigned int dockspaceId);
        void drawMainMenu();
        void drawAboutPopup();

        EditorCamera m_camera;
        EditorGrid m_grid;
        std::vector<std::unique_ptr<Panel>> m_panels;
        bool m_showImGuiDemo = false;
        bool m_layoutChecked = false;
        bool m_resetLayout = false;
        bool m_openAbout = false;
    };

    /// Draws the editor's overlays over the scene. A module of its own because modules render in
    /// the order they were added: the EditorModule comes before the scene's modules (see above), and
    /// this one after all of them.
    class EditorOverlayModule final : public Module
    {
    public:
        EditorOverlayModule(Application& app, EditorModule& editor) : Module(app), m_editor(editor) {}

        [[nodiscard]] std::string_view name() const override { return "Editor Overlays"; }

        void render() override { m_editor.renderSceneOverlays(); }

    private:
        EditorModule& m_editor;
    };
}

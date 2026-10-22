#pragma once

#include "Application/Module.h"
#include "EditorFiles.h"
#include "EditorSelection.h"
#include "Panels/Panel.h"
#include "Platform/PlatformEvents.h"
#include "Scene/EditorCamera.h"
#include "Scene/EditorGrid.h"
#include "Scene/SelectionOutline.h"

#include <array>
#include <memory>
#include <string>
#include <vector>

namespace arc
{
    class AssetModule;
    class ImGuiModule;
    class SceneModule;

    /// The editor: main menu, dockable panels, the selection, global shortcuts, and the Scene view's
    /// camera and grid. Imports the sample model into the scene at start-up, and any .gltf or .glb
    /// file chosen in File > Import Model, dropped on the window, or dragged from the Files panel.
    /// Models inside a mounted folder are imported by their virtual path, which is also what the
    /// recent models list in user:// keeps.
    ///
    /// Must be added after the SceneModule and before any module with UI: the dock space has to
    /// exist before the windows it hosts are submitted. The scene camera is handed to the Renderer
    /// in update(), so it applies to the whole frame's rendering. The grid and the selection outline are
    /// drawn by EditorOverlayModule, which comes after the modules that draw the scene. Camera controls are listed in EditorCamera.h.
    ///
    ///   F5          Play / Pause
    ///   Shift+F5    Stop
    ///   F6          Step one frame
    ///   + / -       Double / halve the time scale
    ///   0           Reset the time scale to 1
    ///   F11         Capture a GPU frame (RenderDoc, PIX or Xcode, when attached)
    ///   Delete      Delete the selected object
    ///   Ctrl+D      Duplicate the selected object (Cmd+D on macOS)
    class EditorModule final : public Module, public PlatformEventListener
    {
    public:
        /// `imgui` gives panels ImTextureIDs for engine textures, such as the scene target; `assets`
        /// backs the Files panel.
        EditorModule(Application& app, ImGuiModule& imgui, AssetModule& assets, SceneModule& scene);
        ~EditorModule() override;

        [[nodiscard]] std::string_view name() const override { return "Editor"; }

        [[nodiscard]] bool init() override;
        void update(const FrameTime& time) override;
        void shutdown() override;

        void onPlatformEvent(const SDL_Event& event) override;

        /// Draws over the scene: the grid, then the selection outline. Called by EditorOverlayModule.
        void renderSceneOverlays();

    private:
        void handleShortcuts();
        void requestGpuCapture();
        void logInputActivity();
        void drawDockSpace();
        void buildDefaultLayout(unsigned int dockspaceId);
        void drawMainMenu();
        void drawGameObjectMenu();
        void drawRecentModelsMenu();
        void openImportDialog();
        void drawImportPopup();
        void drawAboutPopup();
        /// `remember` adds the model to the recent models list.
        void importModel(const std::string& path, bool remember = true);
        void frameSelection();

        SceneModule& m_scene;
        EditorSelection m_selection;
        EditorCamera m_camera;
        EditorGrid m_grid;
        SelectionOutline m_outline;
        std::vector<std::unique_ptr<Panel>> m_panels;
        bool m_showImGuiDemo = false;
        bool m_layoutChecked = false;
        bool m_resetLayout = false;
        bool m_openAbout = false;
        bool m_openImport = false;
        std::string m_pendingImport;
        std::array<char, 1024> m_importPath{};
        RecentFiles m_recentModels{"user://RecentModels.txt", 10};
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

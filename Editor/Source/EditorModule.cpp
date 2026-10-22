#include "EditorModule.h"

#include "Application/Application.h"
#include "Core/Log.h"
#include "Panels/ConsolePanel.h"
#include "Panels/FilesPanel.h"
#include "Panels/HierarchyPanel.h"
#include "Panels/InspectorPanel.h"
#include "Panels/PerformancePanel.h"
#include "Panels/PlaybackPanel.h"
#include "Panels/ScenePanel.h"
#include "Panels/SystemInfoPanel.h"
#include "Platform/FileSystem.h"
#include "RHI/Device.h"
#include "Scene/MeshComponent.h"
#include "Scene/SceneModule.h"
#include "Scene/TransformGizmo.h"

#include <SDL3/SDL_events.h>
#include <imgui.h>
#include <imgui_internal.h>

#include <algorithm>
#include <cstring>
#include <format>
#include <string>
#include <string_view>

namespace arc
{
    namespace
    {
        // Windows that are not panels but are part of the default layout.
        constexpr const char* kOtherLayoutWindows[] = {"Texture Demo"};

        bool hasSavedSettings(const char* windowTitle)
        {
            return ImGui::FindWindowSettingsByID(ImHashStr(windowTitle)) != nullptr;
        }
    }

    EditorModule::EditorModule(Application& app, ImGuiModule& imgui, AssetModule& assets, SceneModule& scene)
        : Module(app)
        , m_scene(scene)
        , m_selection(scene)
    {
        auto scenePanel = std::make_unique<ScenePanel>(app, imgui, scene, m_selection, m_camera, m_grid, m_outline);
        scenePanel->onModelDropped = [this](const std::string& path) { m_pendingImport = path; };
        m_panels.push_back(std::move(scenePanel));
        m_panels.push_back(std::make_unique<HierarchyPanel>(scene, m_selection, m_camera));
        m_panels.push_back(std::make_unique<InspectorPanel>(scene, m_selection));
        m_panels.push_back(std::make_unique<PlaybackPanel>(app));
        m_panels.push_back(std::make_unique<ConsolePanel>());
        m_panels.push_back(std::make_unique<FilesPanel>(
            app.fileSystem(), assets, FilesPanel::Actions{
                                  .importModel = [this](const std::string& path) { m_pendingImport = path; },
                                  .loadSkybox = [this](const std::string& folder) {
                                      m_scene.skybox().load(this->app().renderer(), folder);
                                  },
                              }));
        m_panels.push_back(std::make_unique<PerformancePanel>(app));
        m_panels.push_back(std::make_unique<SystemInfoPanel>(app));
    }

    // Defined here, where Panel's derived types are complete, for the unique_ptr destructors.
    EditorModule::~EditorModule() = default;

    bool EditorModule::init()
    {
        if (!m_grid.init(app().renderer()) || !m_outline.init(app().renderer()))
            return false;
        app().addEventListener(*this);

        m_recentModels.load(app().fileSystem());

        const std::string samplePath = "assets://Models/Lantern.glb";
        if (!app().fileSystem().isFile(samplePath))
            ARC_LOG_WARN("{} not found; re-run CMake to download it, or drop a glTF file on the window", samplePath);
        else
            importModel(samplePath, false);
        return true;
    }

    void EditorModule::shutdown()
    {
        app().removeEventListener(*this);
        m_outline.shutdown();
        m_grid.shutdown();
    }

    void EditorModule::onPlatformEvent(const SDL_Event& event)
    {
        // Imported in update(), outside the event pump and outside any frame.
        if (event.type == SDL_EVENT_DROP_FILE && event.drop.data)
            m_pendingImport = event.drop.data;
    }

    void EditorModule::importModel(const std::string& path, bool remember)
    {
        // One path per file, so the model cache shares a model dropped from Explorer with the same
        // model imported from the Files panel.
        const std::string stored = preferredPath(app().fileSystem(), path);
        GameObject* object = m_scene.importModel(stored);
        if (!object)
            return;
        m_selection.select(object);
        frameSelection();
        if (remember)
            m_recentModels.add(app().fileSystem(), stored);

        const size_t length = std::min(stored.size(), m_importPath.size() - 1);
        std::memcpy(m_importPath.data(), stored.data(), length);
        m_importPath[length] = '\0';
    }

    void EditorModule::frameSelection()
    {
        if (const GameObject* selected = m_selection.object())
            m_camera.frame(hierarchyBounds(*selected), selected->transform().worldPosition());
    }

    void EditorModule::renderSceneOverlays()
    {
        m_grid.render(app().renderer());
        m_outline.render(app().renderer(), m_selection.object());
    }

    void EditorModule::update(const FrameTime& /*time*/)
    {
        if (!m_pendingImport.empty())
        {
            const std::string path = std::move(m_pendingImport);
            m_pendingImport.clear();
            importModel(path);
        }

        handleShortcuts();
        logInputActivity();

        TransformGizmo::beginFrame();
        drawMainMenu();
        drawDockSpace();
        for (auto& panel : m_panels)
            if (panel->isOpen)
                panel->draw(&panel->isOpen);

        if (m_showImGuiDemo)
            ImGui::ShowDemoWindow(&m_showImGuiDemo);

        // After the Scene panel, which moves the camera. Also when the panel is closed, so settings
        // keep applying.
        app().renderer().setSceneCamera(m_camera.camera());
        // The Scene panel has given the scene target its size for this frame.
        m_outline.prepare(app().renderer());

        drawImportPopup();
        drawAboutPopup();
    }

    void EditorModule::requestGpuCapture()
    {
        rhi::Device& device = app().renderer().device();
        if (device.gpuCaptureTool().empty())
        {
            ARC_LOG_WARN("No GPU capture tool attached; start the editor from RenderDoc, with --pix, or from Xcode");
            return;
        }
        device.captureNextFrame();
    }

    void EditorModule::handleShortcuts()
    {
        const Input& input = app().input();
        GameClock& clock = app().gameClock();

        if (input.wasPressed(Key::F5) || input.wasPressed(GamepadButton::Start))
        {
            if (input.isShiftDown())
                clock.stop();
            else if (clock.state() == GameClock::State::Playing)
                clock.pause();
            else
                clock.play();
            ARC_LOG_INFO("Game clock: {}", GameClock::stateName(clock.state()));
        }

        if (input.wasPressed(Key::F6))
            clock.step();

        if (input.wasPressed(Key::F11))
            requestGpuCapture();

        // Through ImGui rather than Input: Input hides the keyboard while a panel has focus, and
        // these must work from the Hierarchy too. Not while a text field is being edited, where
        // Delete and Ctrl+D edit the text.
        if (!ImGui::GetIO().WantTextInput && m_selection.object())
        {
            if (ImGui::IsKeyPressed(ImGuiKey_Delete, false))
                m_selection.destroySelected();
            else if (ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_D))
                m_selection.duplicateSelected();
        }

        // Equals is the unshifted '+' key on US and most European layouts.
        if (input.wasPressed(Key::Equals))
            clock.setTimeScale(clock.timeScale() * 2.0f);
        if (input.wasPressed(Key::Minus))
            clock.setTimeScale(clock.timeScale() * 0.5f);
        if (input.wasPressed(Key::Num0))
            clock.setTimeScale(1.0f);
    }

    void EditorModule::logInputActivity()
    {
        const Input& input = app().input();

        if (input.wasPressed(MouseButton::Left))
        {
            [[maybe_unused]] const Vec2 position = input.mousePosition();
            ARC_LOG_TRACE("Left click at ({:.0f}, {:.0f}) px", position.x, position.y);
        }

        if (input.wasPressed(GamepadButton::South))
        {
            [[maybe_unused]] const Vec2 stick = input.leftStick();
            ARC_LOG_TRACE("Gamepad South pressed; left stick ({:+.2f}, {:+.2f})", stick.x, stick.y);
        }
    }

    void EditorModule::drawDockSpace()
    {
        const ImGuiID dockspaceId = ImGui::GetID("EditorDockSpace");

        // A dockspace with no splits means there is no saved layout in imgui.ini (first launch).
        if (!m_layoutChecked || m_resetLayout)
        {
            const ImGuiDockNode* node = ImGui::DockBuilderGetNode(dockspaceId);
            // A window with no saved settings is one added since imgui.ini was written; the layout is
            // rebuilt once so it gets a place in the dock space instead of floating.
            bool missingWindow = std::any_of(m_panels.begin(), m_panels.end(),
                                             [](const auto& panel) { return !hasSavedSettings(panel->title()); });
            for (const char* window : kOtherLayoutWindows)
                missingWindow = missingWindow || !hasSavedSettings(window);
            if (m_resetLayout || node == nullptr || node->IsLeafNode() || missingWindow)
                buildDefaultLayout(dockspaceId);
            m_layoutChecked = true;
            m_resetLayout = false;
        }

        ImGui::DockSpaceOverViewport(dockspaceId);
    }

    void EditorModule::buildDefaultLayout(unsigned int dockspaceId)
    {
        const ImGuiViewport* viewport = ImGui::GetMainViewport();

        ImGui::DockBuilderRemoveNode(dockspaceId);
        // DockSpace is in ImGui's private flag enum; combining it with a public flag needs the int type.
        const ImGuiDockNodeFlags flags = static_cast<ImGuiDockNodeFlags>(ImGuiDockNodeFlags_DockSpace);
        ImGui::DockBuilderAddNode(dockspaceId, flags);
        ImGui::DockBuilderSetNodeSize(dockspaceId, viewport->WorkSize);

        ImGuiID center = dockspaceId;
        const ImGuiID right = ImGui::DockBuilderSplitNode(center, ImGuiDir_Right, 0.24f, nullptr, &center);
        const ImGuiID left = ImGui::DockBuilderSplitNode(center, ImGuiDir_Left, 0.2f, nullptr, &center);
        const ImGuiID bottom = ImGui::DockBuilderSplitNode(center, ImGuiDir_Down, 0.28f, nullptr, &center);
        const ImGuiID top = ImGui::DockBuilderSplitNode(center, ImGuiDir_Up, 0.08f, nullptr, &center);
        ImGuiID rightTop = right;
        const ImGuiID rightBottom = ImGui::DockBuilderSplitNode(rightTop, ImGuiDir_Down, 0.4f, nullptr, &rightTop);

        ImGui::DockBuilderDockWindow("Scene", center);
        ImGui::DockBuilderDockWindow("Hierarchy", left);
        ImGui::DockBuilderDockWindow("Playback", top);
        ImGui::DockBuilderDockWindow("Console", bottom);
        ImGui::DockBuilderDockWindow("Files", bottom);
        ImGui::DockBuilderDockWindow("Inspector", rightTop);
        ImGui::DockBuilderDockWindow("Performance", rightBottom);
        ImGui::DockBuilderDockWindow("System Info", rightBottom);
        ImGui::DockBuilderDockWindow("Texture Demo", rightBottom);
        ImGui::DockBuilderFinish(dockspaceId);

        for (auto& panel : m_panels)
            panel->isOpen = true;
    }

    void EditorModule::drawMainMenu()
    {
        if (!ImGui::BeginMainMenuBar())
            return;

        if (ImGui::BeginMenu("File"))
        {
            if (ImGui::MenuItem("Import Model..."))
                openImportDialog();
            if (ImGui::MenuItem("Import Model by Path..."))
                m_openImport = true;
            drawRecentModelsMenu();
            ImGui::Separator();
            if (ImGui::MenuItem("Exit"))
                app().requestQuit();
            ImGui::EndMenu();
        }

        drawGameObjectMenu();

        if (ImGui::BeginMenu("View"))
        {
            for (auto& panel : m_panels)
                ImGui::MenuItem(panel->title(), nullptr, &panel->isOpen);
            ImGui::Separator();
            ImGui::MenuItem("ImGui Demo", nullptr, &m_showImGuiDemo);
            ImGui::Separator();
            if (ImGui::MenuItem("Reset Layout"))
                m_resetLayout = true;
            ImGui::EndMenu();
        }

        if (ImGui::BeginMenu("Debug"))
        {
            const std::string_view tool = app().renderer().device().gpuCaptureTool();
            const std::string label = tool.empty() ? std::string("Capture GPU Frame")
                                                   : std::format("Capture GPU Frame ({})", tool);
            if (ImGui::MenuItem(label.c_str(), "F11", false, !tool.empty()))
                requestGpuCapture();
            if (tool.empty())
                ImGui::SetItemTooltip("No capture tool attached. Start the editor from RenderDoc, with --pix "
                                      "(PIX), or from Xcode.");
            ImGui::EndMenu();
        }

        if (ImGui::BeginMenu("Help"))
        {
            if (ImGui::MenuItem("About Arclight"))
                m_openAbout = true;
            ImGui::EndMenu();
        }

        ImGui::EndMainMenuBar();
    }

    void EditorModule::drawGameObjectMenu()
    {
        if (!ImGui::BeginMenu("GameObject"))
            return;
        GameObject* selected = m_selection.object();
        if (ImGui::MenuItem("Create Empty"))
            m_selection.createEmpty(nullptr);
        if (ImGui::MenuItem("Create Empty Child", nullptr, false, selected != nullptr))
            m_selection.createEmpty(selected);
        ImGui::Separator();
        if (ImGui::MenuItem("Duplicate", "Ctrl+D", false, selected != nullptr))
            m_selection.duplicateSelected();
        if (ImGui::MenuItem("Delete", "Del", false, selected != nullptr))
            m_selection.destroySelected();
        ImGui::Separator();
        if (ImGui::MenuItem("Frame Selected", "F", false, selected != nullptr))
            frameSelection();
        ImGui::EndMenu();
    }

    void EditorModule::drawRecentModelsMenu()
    {
        const std::vector<std::string>& recent = m_recentModels.paths();
        if (!ImGui::BeginMenu("Recent Models", !recent.empty()))
            return;
        std::string chosen;
        for (const std::string& path : recent)
            if (ImGui::MenuItem(path.c_str(), nullptr, false, fileExists(path)))
                chosen = path;
        ImGui::Separator();
        if (ImGui::MenuItem("Clear List"))
            m_recentModels.clear(app().fileSystem());
        ImGui::EndMenu();
        if (!chosen.empty())
            m_pendingImport = chosen;
    }

    void EditorModule::openImportDialog()
    {
        const VirtualFileSystem& fileSystem = app().fileSystem();
        std::string location;
        if (!m_recentModels.paths().empty())
        {
            const std::string& last = m_recentModels.paths().front();
            location = parentPath(isVirtualPath(last) ? fileSystem.nativePath(last) : last);
        }
        if (location.empty())
            location = fileSystem.nativePath("assets://Models");

        FileDialogDesc desc{
            .type = FileDialogType::OpenFile,
            .title = "Import Model",
            .filters = {{"glTF models", "gltf;glb"}, {"All files", "*"}},
            .defaultLocation = location,
        };
        app().fileDialogs().open(app().window().sdlWindow(), std::move(desc), [this](const FileDialogResult& result) {
            if (result.status == FileDialogResult::Status::Chosen)
            {
                m_pendingImport = result.paths.front();
            }
            else if (result.status == FileDialogResult::Status::Failed)
            {
                // Linux without a desktop portal or zenity, for example.
                ARC_LOG_WARN("The file dialog is not available ({}); enter the path instead", result.error);
                m_openImport = true;
            }
        });
    }

    void EditorModule::drawImportPopup()
    {
        if (m_openImport)
        {
            ImGui::OpenPopup("Import Model by Path");
            m_openImport = false;
        }

        ImGui::SetNextWindowSize({520.0f, 0.0f}, ImGuiCond_Appearing);
        if (!ImGui::BeginPopupModal("Import Model by Path", nullptr))
            return;
        ImGui::TextUnformatted("Path to a .gltf or .glb file, native or virtual (assets://...):");
        if (ImGui::IsWindowAppearing())
            ImGui::SetKeyboardFocusHere();
        ImGui::SetNextItemWidth(-1.0f);
        const bool submitted =
            ImGui::InputText("##Path", m_importPath.data(), m_importPath.size(), ImGuiInputTextFlags_EnterReturnsTrue);
        ImGui::TextDisabled("Or drop the file on the window.");
        if (ImGui::Button("Import") || submitted)
        {
            m_pendingImport = m_importPath.data();
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel") || ImGui::IsKeyPressed(ImGuiKey_Escape))
            ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }

    void EditorModule::drawAboutPopup()
    {
        // OpenPopup must be called from the same ID stack as BeginPopupModal, not from inside the menu.
        if (m_openAbout)
        {
            ImGui::OpenPopup("About Arclight");
            m_openAbout = false;
        }

        if (ImGui::BeginPopupModal("About Arclight", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
        {
            ImGui::Text("Arclight %s", ARCLIGHT_VERSION);
            ImGui::TextDisabled("A cross-platform learning engine: D3D12 on Windows, Metal on macOS.");
            ImGui::Separator();
            ImGui::Text("Dear ImGui %s", IMGUI_VERSION);
            if (ImGui::Button("Close"))
                ImGui::CloseCurrentPopup();
            ImGui::EndPopup();
        }
    }
}

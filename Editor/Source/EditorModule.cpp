#include "EditorModule.h"

#include "Application/Application.h"
#include "Core/Log.h"
#include "Panels/ConsolePanel.h"
#include "Panels/PerformancePanel.h"
#include "Panels/PlaybackPanel.h"
#include "Panels/ScenePanel.h"
#include "Panels/SystemInfoPanel.h"

#include "RHI/Device.h"

#include <imgui.h>
#include <imgui_internal.h>

#include <format>
#include <string>
#include <string_view>

namespace arc
{
    EditorModule::EditorModule(Application& app, ImGuiModule& imgui)
        : Module(app)
    {
        m_panels.push_back(std::make_unique<ScenePanel>(app, imgui, m_camera, m_grid));
        m_panels.push_back(std::make_unique<PlaybackPanel>(app));
        m_panels.push_back(std::make_unique<ConsolePanel>());
        m_panels.push_back(std::make_unique<PerformancePanel>(app));
        m_panels.push_back(std::make_unique<SystemInfoPanel>(app));
    }

    // Defined here, where Panel's derived types are complete, for the unique_ptr destructors.
    EditorModule::~EditorModule() = default;

    bool EditorModule::init()
    {
        return m_grid.init(app().renderer());
    }

    void EditorModule::shutdown()
    {
        m_grid.shutdown();
    }

    void EditorModule::renderSceneOverlays()
    {
        m_grid.render(app().renderer());
    }

    void EditorModule::update(const FrameTime& /*time*/)
    {
        handleShortcuts();
        logInputActivity();

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
            if (m_resetLayout || node == nullptr || node->IsLeafNode())
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
        const ImGuiID bottom = ImGui::DockBuilderSplitNode(center, ImGuiDir_Down, 0.28f, nullptr, &center);
        const ImGuiID top = ImGui::DockBuilderSplitNode(center, ImGuiDir_Up, 0.08f, nullptr, &center);
        ImGuiID rightTop = right;
        const ImGuiID rightBottom = ImGui::DockBuilderSplitNode(rightTop, ImGuiDir_Down, 0.45f, nullptr, &rightTop);

        ImGui::DockBuilderDockWindow("Scene", center);
        ImGui::DockBuilderDockWindow("Playback", top);
        ImGui::DockBuilderDockWindow("Console", bottom);
        ImGui::DockBuilderDockWindow("Texture Demo", rightTop);
        ImGui::DockBuilderDockWindow("Performance", rightBottom);
        ImGui::DockBuilderDockWindow("System Info", rightBottom);
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
            if (ImGui::MenuItem("Exit"))
                app().requestQuit();
            ImGui::EndMenu();
        }

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

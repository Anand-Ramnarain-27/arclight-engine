#include "EditorModule.h"

#include "Application/Application.h"
#include "Core/Log.h"
#include "Panels/ConsolePanel.h"
#include "Panels/PerformancePanel.h"
#include "Panels/PlaybackPanel.h"
#include "Panels/SystemInfoPanel.h"

#include <imgui.h>
#include <imgui_internal.h>

namespace arc
{
    EditorModule::EditorModule(Application& app)
        : Module(app)
    {
        m_panels.push_back(std::make_unique<PlaybackPanel>(app));
        m_panels.push_back(std::make_unique<ConsolePanel>());
        m_panels.push_back(std::make_unique<PerformancePanel>(app));
        m_panels.push_back(std::make_unique<SystemInfoPanel>(app));
    }

    // Defined here, where Panel's derived types are complete, for the unique_ptr destructors.
    EditorModule::~EditorModule() = default;

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

        drawAboutPopup();
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
            const Vec2 position = input.mousePosition();
            ARC_LOG_TRACE("Left click at ({:.0f}, {:.0f}) px", position.x, position.y);
        }

        if (input.wasPressed(GamepadButton::South))
        {
            const Vec2 stick = input.leftStick();
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

        // The central node is left transparent so the scene viewport shows through and mouse
        // input over it reaches the game instead of ImGui.
        ImGui::DockSpaceOverViewport(dockspaceId, nullptr, ImGuiDockNodeFlags_PassthruCentralNode);
    }

    void EditorModule::buildDefaultLayout(unsigned int dockspaceId)
    {
        const ImGuiViewport* viewport = ImGui::GetMainViewport();

        ImGui::DockBuilderRemoveNode(dockspaceId);
        // DockSpace is in ImGui's private flag enum; combining it with a public flag needs the int type.
        const ImGuiDockNodeFlags flags = static_cast<ImGuiDockNodeFlags>(ImGuiDockNodeFlags_DockSpace) |
                                         ImGuiDockNodeFlags_PassthruCentralNode;
        ImGui::DockBuilderAddNode(dockspaceId, flags);
        ImGui::DockBuilderSetNodeSize(dockspaceId, viewport->WorkSize);

        ImGuiID center = dockspaceId;
        const ImGuiID right = ImGui::DockBuilderSplitNode(center, ImGuiDir_Right, 0.24f, nullptr, &center);
        const ImGuiID bottom = ImGui::DockBuilderSplitNode(center, ImGuiDir_Down, 0.28f, nullptr, &center);
        const ImGuiID top = ImGui::DockBuilderSplitNode(center, ImGuiDir_Up, 0.08f, nullptr, &center);

        ImGui::DockBuilderDockWindow("Playback", top);
        ImGui::DockBuilderDockWindow("Console", bottom);
        ImGui::DockBuilderDockWindow("Performance", right);
        ImGui::DockBuilderDockWindow("System Info", right);
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

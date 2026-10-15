#pragma once

#include "Application/Module.h"
#include "Panels/Panel.h"

#include <memory>
#include <vector>

namespace arc
{
    /// The editor: main menu, dockable panels and global shortcuts.
    ///
    ///   F5          Play / Pause
    ///   Shift+F5    Stop
    ///   F6          Step one frame
    ///   + / -       Double / halve the time scale
    ///   0           Reset the time scale to 1
    ///   F11         Capture a GPU frame (RenderDoc, PIX or Xcode, when attached)
    class ImGuiModule;

    class EditorModule final : public Module
    {
    public:
        /// `imgui` gives panels ImTextureIDs for engine textures, such as the scene target.
        EditorModule(Application& app, ImGuiModule& imgui);
        ~EditorModule() override;

        [[nodiscard]] std::string_view name() const override { return "Editor"; }

        void update(const FrameTime& time) override;

    private:
        void handleShortcuts();
        void requestGpuCapture();
        void logInputActivity();
        void drawDockSpace();
        void buildDefaultLayout(unsigned int dockspaceId);
        void drawMainMenu();
        void drawAboutPopup();

        std::vector<std::unique_ptr<Panel>> m_panels;
        bool m_showImGuiDemo = false;
        bool m_layoutChecked = false;
        bool m_resetLayout = false;
        bool m_openAbout = false;
    };
}

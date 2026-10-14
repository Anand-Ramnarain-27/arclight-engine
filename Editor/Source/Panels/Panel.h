#pragma once

namespace arc
{
    /// A dockable editor window. The EditorModule owns panels and lists them in the View menu.
    class Panel
    {
    public:
        virtual ~Panel() = default;

        /// Also the ImGui window ID, so it must be unique and stable: imgui.ini stores layout by it.
        [[nodiscard]] virtual const char* title() const = 0;

        /// Called every frame while the panel is open. Must call ImGui::Begin/End itself
        /// with `open` so the window's close button works.
        virtual void draw(bool* open) = 0;

        bool isOpen = true;
    };
}

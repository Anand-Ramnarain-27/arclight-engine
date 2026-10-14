#pragma once

#include <cstdint>
#include <memory>
#include <string>

struct SDL_Window;

namespace arc
{
    struct WindowDesc
    {
        std::string title = "Arclight";
        uint32_t width = 1600;   // Points, not pixels.
        uint32_t height = 900;   // Points, not pixels.
        bool resizable = true;
        bool maximized = false;
    };

    struct Extent2D
    {
        uint32_t width = 0;
        uint32_t height = 0;
    };

    class Window
    {
    public:
        /// Returns nullptr and logs the reason on failure. SDL video must already be initialised.
        [[nodiscard]] static std::unique_ptr<Window> create(const WindowDesc& desc);

        ~Window();

        Window(const Window&) = delete;
        Window& operator=(const Window&) = delete;
        Window(Window&&) = delete;
        Window& operator=(Window&&) = delete;

        /// Drawable size in pixels. Differs from the size in points on HiDPI displays.
        [[nodiscard]] Extent2D pixelSize() const;
        [[nodiscard]] bool isMinimized() const;

        void setTitle(const std::string& title);

        /// Engine-internal: for event routing, the renderer and the ImGui backend.
        [[nodiscard]] SDL_Window* sdlWindow() const { return m_window; }

    private:
        explicit Window(SDL_Window* window) : m_window(window) {}

        SDL_Window* m_window = nullptr;
    };
}

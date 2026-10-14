#include "Platform/Window.h"

#include "Core/Assert.h"
#include "Core/Log.h"

#include <SDL3/SDL.h>

namespace arc
{
    std::unique_ptr<Window> Window::create(const WindowDesc& desc)
    {
        ARC_ASSERT(SDL_WasInit(SDL_INIT_VIDEO), "SDL video must be initialised before creating a window");

        // Without HIGH_PIXEL_DENSITY, macOS renders at point resolution and upscales the result.
        SDL_WindowFlags flags = SDL_WINDOW_HIGH_PIXEL_DENSITY;
        if (desc.resizable)
            flags |= SDL_WINDOW_RESIZABLE;
        if (desc.maximized)
            flags |= SDL_WINDOW_MAXIMIZED;

        SDL_Window* window = SDL_CreateWindow(desc.title.c_str(),
                                              static_cast<int>(desc.width),
                                              static_cast<int>(desc.height), flags);
        if (!window)
        {
            ARC_LOG_ERROR("SDL_CreateWindow failed: {}", SDL_GetError());
            return nullptr;
        }

        // std::make_unique cannot call the private constructor.
        auto result = std::unique_ptr<Window>(new Window(window));

        const Extent2D size = result->pixelSize();
        ARC_LOG_INFO("Window created: {}x{} px (scale {:.2f})", size.width, size.height,
                     SDL_GetWindowDisplayScale(window));
        return result;
    }

    Window::~Window()
    {
        if (m_window)
            SDL_DestroyWindow(m_window);
    }

    Extent2D Window::pixelSize() const
    {
        int width = 0;
        int height = 0;
        SDL_GetWindowSizeInPixels(m_window, &width, &height);
        return {static_cast<uint32_t>(width), static_cast<uint32_t>(height)};
    }

    bool Window::isMinimized() const
    {
        return (SDL_GetWindowFlags(m_window) & SDL_WINDOW_MINIMIZED) != 0;
    }

    void Window::setTitle(const std::string& title)
    {
        SDL_SetWindowTitle(m_window, title.c_str());
    }
}

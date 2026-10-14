#include "Application/Application.h"

#include "Core/Assert.h"
#include "Core/Log.h"

#include <SDL3/SDL.h>

namespace arc
{
    Application::Application(ApplicationDesc desc)
        : m_desc(std::move(desc))
    {
    }

    Application::~Application()
    {
        // No-op after a normal run(); cleans up if run() was never called or exited early.
        shutdown();
    }

    int Application::run()
    {
        if (!init())
        {
            ARC_LOG_FATAL("Initialisation failed, exiting.");
            shutdown();
            return 1;
        }

        ARC_LOG_INFO("Entering main loop");

        while (!m_quitRequested)
        {
            pumpEvents();

            if (m_paused)
            {
                // Block on the event queue instead of spinning while there is nothing to draw.
                SDL_WaitEventTimeout(nullptr, 100);
                continue;
            }

            for (auto& module : m_modules) module->update();
            for (auto& module : m_modules) module->preRender();
            for (auto& module : m_modules) module->render();
            for (auto& module : m_modules) module->postRender();
        }

        ARC_LOG_INFO("Leaving main loop");
        shutdown();
        return 0;
    }

    bool Application::init()
    {
        const int sdlVersion = SDL_GetVersion();
        ARC_LOG_INFO("Arclight starting (SDL {}.{}.{})", SDL_VERSIONNUM_MAJOR(sdlVersion),
                     SDL_VERSIONNUM_MINOR(sdlVersion), SDL_VERSIONNUM_MICRO(sdlVersion));

        if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMEPAD))
        {
            ARC_LOG_ERROR("SDL_Init failed: {}", SDL_GetError());
            return false;
        }
        m_platformInitialized = true;

        m_window = Window::create(m_desc.window);
        if (!m_window)
            return false;

        for (auto& module : m_modules)
        {
            ARC_LOG_TRACE("Initialising module '{}'", module->name());
            if (!module->init())
            {
                ARC_LOG_ERROR("Module '{}' failed to initialise", module->name());
                return false;
            }
            ++m_initializedModules;
        }

        return true;
    }

    void Application::shutdown()
    {
        while (m_initializedModules > 0)
        {
            Module& module = *m_modules[--m_initializedModules];
            ARC_LOG_TRACE("Shutting down module '{}'", module.name());
            module.shutdown();
        }
        m_modules.clear();

        // SDL_DestroyWindow is invalid after SDL_Quit.
        m_window.reset();

        if (m_platformInitialized)
        {
            SDL_Quit();
            m_platformInitialized = false;
            ARC_LOG_INFO("Shutdown complete");
        }
    }

    void Application::pumpEvents()
    {
        SDL_Event event;
        while (SDL_PollEvent(&event))
        {
            switch (event.type)
            {
                case SDL_EVENT_QUIT:
                case SDL_EVENT_WINDOW_CLOSE_REQUESTED:
                    requestQuit();
                    break;

                case SDL_EVENT_WINDOW_MINIMIZED:
                    m_paused = true;
                    break;

                case SDL_EVENT_WINDOW_RESTORED:
                case SDL_EVENT_WINDOW_MAXIMIZED:
                    m_paused = false;
                    break;

                case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED:
                    // TODO: Forward to the renderer to resize the swap chain once the RHI exists.
                    ARC_LOG_TRACE("Window resized to {}x{} px", event.window.data1, event.window.data2);
                    break;

                default:
                    break;
            }
        }
    }
}

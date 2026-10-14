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
        const Stopwatch initTimer;
        if (!init())
        {
            ARC_LOG_FATAL("Initialisation failed, exiting.");
            shutdown();
            return 1;
        }
        ARC_LOG_INFO("Initialised in {:.1f} ms", initTimer.elapsedMs());

        Clock::time_point previousFrameStart = Clock::now();

        while (!m_quitRequested)
        {
            const Clock::time_point frameStart = Clock::now();

            m_input.beginFrame();
            pumpEvents();

            if (m_minimized)
            {
                // Block on the event queue instead of spinning while there is nothing to draw.
                SDL_WaitEventTimeout(nullptr, 100);
                // Time spent minimised must not arrive as one huge delta when the window is restored.
                previousFrameStart = Clock::now();
                continue;
            }

            // Measured start-to-start, so the delta includes the frame-limiter wait.
            runFrame(frameStart - previousFrameStart);
            previousFrameStart = frameStart;

            waitForFrameBudget(frameStart);
        }

        const Stopwatch shutdownTimer;
        shutdown();
        ARC_LOG_INFO("Shut down in {:.1f} ms", shutdownTimer.elapsedMs());
        return 0;
    }

    void Application::runFrame(Clock::duration realDelta)
    {
        m_frameTime = m_gameClock.tick(realDelta);
        m_frameStats.addFrame(m_frameTime.realDelta * 1000.0f);

        for (auto& module : m_modules) module->update(m_frameTime);
        for (auto& module : m_modules) module->preRender();
        for (auto& module : m_modules) module->render();
        for (auto& module : m_modules) module->postRender();
    }

    void Application::waitForFrameBudget(Clock::time_point frameStart) const
    {
        if (m_desc.maxFrameRate == 0)
            return;

        const auto budget = std::chrono::duration_cast<Clock::duration>(Seconds(1.0 / m_desc.maxFrameRate));
        const auto remaining = frameStart + budget - Clock::now();
        if (remaining <= Clock::duration::zero())
            return;

        // A plain OS sleep can overshoot by several milliseconds (Windows timer granularity).
        // SDL_DelayPrecise sleeps for most of the interval and spins for the remainder.
        const auto ns = std::chrono::duration_cast<std::chrono::nanoseconds>(remaining).count();
        SDL_DelayPrecise(static_cast<Uint64>(ns));
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
            const Stopwatch moduleTimer;
            if (!module->init())
            {
                ARC_LOG_ERROR("Module '{}' failed to initialise", module->name());
                return false;
            }
            ++m_initializedModules;
            ARC_LOG_TRACE("Module '{}' initialised in {:.2f} ms", module->name(), moduleTimer.elapsedMs());
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

        if (!m_platformInitialized)
            return;

        // Gamepads and windows are invalid after SDL_Quit.
        m_input.shutdown();
        m_window.reset();

        SDL_Quit();
        m_platformInitialized = false;
    }

    void Application::pumpEvents()
    {
        SDL_Window* sdlWindow = m_window ? m_window->sdlWindow() : nullptr;

        SDL_Event event;
        while (SDL_PollEvent(&event))
        {
            // TODO: Let the ImGui backend see events first and skip game input when it wants capture.
            m_input.handleEvent(event, sdlWindow);

            switch (event.type)
            {
                case SDL_EVENT_QUIT:
                case SDL_EVENT_WINDOW_CLOSE_REQUESTED:
                    requestQuit();
                    break;

                case SDL_EVENT_WINDOW_MINIMIZED:
                    m_minimized = true;
                    break;

                case SDL_EVENT_WINDOW_RESTORED:
                case SDL_EVENT_WINDOW_MAXIMIZED:
                    m_minimized = false;
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

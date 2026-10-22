#include "Application/Application.h"

#include "Core/Assert.h"
#include "Core/Log.h"

#include <SDL3/SDL.h>

#include <algorithm>

namespace arc
{
    Application::Application(ApplicationDesc desc)
        : m_desc(std::move(desc))
    {
        setActiveFileSystem(&m_fileSystem);
    }

    Application::~Application()
    {
        // No-op after a normal run(); cleans up if run() was never called or exited early.
        shutdown();
        if (activeFileSystem() == &m_fileSystem)
            setActiveFileSystem(nullptr);
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
            if (!m_minimized)
                m_renderer->waitUntilReady();

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
        m_fileDialogs.dispatch();

        m_frameTime = m_gameClock.tick(realDelta);
        m_frameStats.addFrame(m_frameTime.realDelta * 1000.0f);

        for (auto& module : m_modules) module->update(m_frameTime);
        for (auto& module : m_modules) module->preRender();

        // render() and postRender() run even when no frame could begin, so modules with
        // per-frame bookkeeping (ImGui must end every frame it starts) stay balanced.
        const bool frameActive = m_renderer->beginFrame();
        for (auto& module : m_modules) module->render();
        if (frameActive)
            m_renderer->finishScene();
        for (auto& module : m_modules) module->postRender();
        if (frameActive)
            m_renderer->endFrame();
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

        // Before the renderer, which loads its shaders through engine://.
        mountDefaultFolders();

        m_window = Window::create(m_desc.window);
        if (!m_window)
            return false;

        m_renderer = std::make_unique<Renderer>();
        if (!m_renderer->init(*m_window, m_desc.device, m_desc.vsync))
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
        // Modules may own GPU resources that in-flight frames still reference.
        if (m_renderer)
            m_renderer->waitIdle();

        while (m_initializedModules > 0)
        {
            Module& module = *m_modules[--m_initializedModules];
            ARC_LOG_TRACE("Shutting down module '{}'", module.name());
            module.shutdown();
        }
        m_modules.clear();

        // The swapchain references the window, so the renderer goes first.
        m_renderer.reset();

        if (!m_platformInitialized)
            return;

        // Gamepads and windows are invalid after SDL_Quit.
        m_input.shutdown();
        m_window.reset();

        SDL_Quit();
        m_platformInitialized = false;
    }

    void Application::mountDefaultFolders()
    {
        const auto isDirectory = [](const std::string& path) {
            SDL_PathInfo info;
            return SDL_GetPathInfo(path.c_str(), &info) && info.type == SDL_PATHTYPE_DIRECTORY;
        };
        const auto mountFolder = [this](const char* name, const std::string& folder, bool writable) {
            m_fileSystem.mount(name, std::make_shared<DirectorySource>(folder, writable));
            ARC_LOG_INFO("Mounted {}:// -> {}{}", name, folder, writable ? "" : " (read-only)");
        };

        const char* basePath = SDL_GetBasePath();
        const std::string executableFolder = basePath ? basePath : "";
        mountFolder("engine", executableFolder, false);

        // The executable's copy first and the working directory's last, so it wins: assets edited
        // in Game/Assets are used without copying them next to the build. The working directory's
        // folder is the project's own, so it is writable, for .meta files; the copy next to the
        // executable is build output and stays read-only.
        const std::string packagedAssets = executableFolder + "Assets";
        if (isDirectory(packagedAssets))
            mountFolder("assets", packagedAssets, false);
        char* currentDirectory = SDL_GetCurrentDirectory();
        const std::string workingDirectory = currentDirectory ? currentDirectory : "";
        SDL_free(currentDirectory);
        const std::string workingAssets = workingDirectory + "Assets";
        const bool projectAssets = workingAssets != packagedAssets && isDirectory(workingAssets);
        if (projectAssets)
            mountFolder("assets", workingAssets, true);
        if (!m_fileSystem.isMounted("assets"))
            ARC_LOG_WARN("No Assets folder next to the executable or in the working directory; assets:// is empty");

        char* preferences = SDL_GetPrefPath(m_desc.organization.c_str(), m_desc.name.c_str());
        const std::string userFolder = preferences ? preferences : "";
        SDL_free(preferences);
        if (!userFolder.empty())
            mountFolder("user", userFolder, true);
        else
            ARC_LOG_WARN("No per-user folder ({}); user:// is not mounted", SDL_GetError());

        // Cooked assets are a cache, rebuilt from the sources at any time: beside the project's
        // assets when there is a project (Game/Library, ignored by git), otherwise per user.
        if (projectAssets)
            mountFolder("library", workingDirectory + "Library", true);
        else if (!userFolder.empty())
            mountFolder("library", userFolder + "Library", true);
    }

    void Application::addEventListener(PlatformEventListener& listener)
    {
        ARC_ASSERT(std::find(m_eventListeners.begin(), m_eventListeners.end(), &listener) == m_eventListeners.end());
        m_eventListeners.push_back(&listener);
    }

    void Application::removeEventListener(PlatformEventListener& listener)
    {
        std::erase(m_eventListeners, &listener);
    }

    void Application::pumpEvents()
    {
        SDL_Window* sdlWindow = m_window ? m_window->sdlWindow() : nullptr;

        SDL_Event event;
        while (SDL_PollEvent(&event))
        {
            for (PlatformEventListener* listener : m_eventListeners)
                listener->onPlatformEvent(event);
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
                    if (m_renderer)
                        m_renderer->onWindowResized({static_cast<uint32_t>(event.window.data1),
                                                     static_cast<uint32_t>(event.window.data2)});
                    break;

                default:
                    break;
            }
        }
    }
}

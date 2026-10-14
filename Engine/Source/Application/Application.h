#pragma once

#include "Application/Module.h"
#include "Platform/Window.h"

#include <memory>
#include <utility>
#include <vector>

namespace arc
{
    struct ApplicationDesc
    {
        WindowDesc window;
    };

    /// Owns the platform layer, the window and all modules, and runs the main loop.
    class Application
    {
    public:
        explicit Application(ApplicationDesc desc);
        ~Application();

        Application(const Application&) = delete;
        Application& operator=(const Application&) = delete;

        /// Must be called before run(). Extra arguments are forwarded after the Application reference.
        template <typename T, typename... Args>
        T& addModule(Args&&... args)
        {
            auto module = std::make_unique<T>(*this, std::forward<Args>(args)...);
            T& ref = *module;
            m_modules.push_back(std::move(module));
            return ref;
        }

        /// Blocks until the application quits. Returns the process exit code.
        [[nodiscard]] int run();

        /// Takes effect at the end of the current frame.
        void requestQuit() { m_quitRequested = true; }

        /// Valid only while run() is executing.
        [[nodiscard]] Window& window() { return *m_window; }

    private:
        [[nodiscard]] bool init();
        void shutdown();
        void pumpEvents();

        ApplicationDesc m_desc;
        std::unique_ptr<Window> m_window;
        std::vector<std::unique_ptr<Module>> m_modules;

        // Count of modules whose init() succeeded; only these are shut down.
        size_t m_initializedModules = 0;

        bool m_platformInitialized = false;
        bool m_quitRequested = false;
        bool m_paused = false;
    };
}

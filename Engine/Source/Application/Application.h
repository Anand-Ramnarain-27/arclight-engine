#pragma once

#include "Application/Module.h"
#include "Core/Time.h"
#include "Platform/Input.h"
#include "Platform/PlatformEvents.h"
#include "Platform/Window.h"
#include "RHI/RHITypes.h"
#include "Renderer/Renderer.h"

#include <cstdint>
#include <memory>
#include <utility>
#include <vector>

namespace arc
{
    struct ApplicationDesc
    {
        WindowDesc window;

        rhi::DeviceDesc device{};
        bool vsync = true;

        /// 0 means unlimited (or paced by vsync).
        uint32_t maxFrameRate = 0;
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

        /// Valid only while run() is executing.
        [[nodiscard]] Renderer& renderer() { return *m_renderer; }

        [[nodiscard]] const Input& input() const { return m_input; }
        [[nodiscard]] Input& input() { return m_input; }
        [[nodiscard]] GameClock& gameClock() { return m_gameClock; }
        [[nodiscard]] const FrameTime& frameTime() const { return m_frameTime; }
        [[nodiscard]] const FrameStats& frameStats() const { return m_frameStats; }

        /// 0 means unlimited.
        void setMaxFrameRate(uint32_t fps) { m_desc.maxFrameRate = fps; }
        [[nodiscard]] uint32_t maxFrameRate() const { return m_desc.maxFrameRate; }

        /// The listener must stay alive until it is removed.
        void addEventListener(PlatformEventListener& listener);
        void removeEventListener(PlatformEventListener& listener);

    private:
        [[nodiscard]] bool init();
        void shutdown();
        void pumpEvents();
        void runFrame(Clock::duration realDelta);
        void waitForFrameBudget(Clock::time_point frameStart) const;

        ApplicationDesc m_desc;
        std::unique_ptr<Window> m_window;
        std::unique_ptr<Renderer> m_renderer;
        std::vector<std::unique_ptr<Module>> m_modules;
        std::vector<PlatformEventListener*> m_eventListeners;

        Input m_input;
        GameClock m_gameClock;
        FrameTime m_frameTime;
        FrameStats m_frameStats;

        // Count of modules whose init() succeeded; only these are shut down.
        size_t m_initializedModules = 0;

        bool m_platformInitialized = false;
        bool m_quitRequested = false;
        bool m_minimized = false;
    };
}

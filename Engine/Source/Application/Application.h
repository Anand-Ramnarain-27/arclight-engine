#pragma once

#include "Application/Module.h"
#include "Core/Time.h"
#include "Platform/FileDialog.h"
#include "Platform/Input.h"
#include "Platform/PlatformEvents.h"
#include "Platform/VirtualFileSystem.h"
#include "Platform/Window.h"
#include "RHI/RHITypes.h"
#include "Renderer/Renderer.h"

#include <cstdint>
#include <memory>
#include <string>
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

        /// Name the per-user folder that is mounted as user://, for example
        /// %APPDATA%/<organization>/<name>/ on Windows and ~/Library/Application Support/<organization>/<name>/ on macOS.
        std::string organization = "Arclight";
        std::string name = "Arclight";
    };

    /// Owns the platform layer, the window and all modules, and runs the main loop.
    ///
    /// Its VirtualFileSystem is the active one while the Application exists, with these mounts:
    ///   engine://  the executable's folder (compiled shaders), read-only
    ///   assets://  Assets/ next to the executable (read-only), overlaid by Assets/ in the working
    ///              directory (Game/ when started from an IDE), which is writable
    ///   user://    the per-user settings folder, writable
    ///   library:// cooked assets: Library/ in the working directory when it has Assets/, otherwise
    ///              Library/ in the per-user folder; writable
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

        [[nodiscard]] VirtualFileSystem& fileSystem() { return m_fileSystem; }
        [[nodiscard]] const VirtualFileSystem& fileSystem() const { return m_fileSystem; }

        /// Results arrive at the start of a frame, before the modules update.
        [[nodiscard]] FileDialogs& fileDialogs() { return m_fileDialogs; }

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
        void mountDefaultFolders();
        void pumpEvents();
        void runFrame(Clock::duration realDelta);
        void waitForFrameBudget(Clock::time_point frameStart) const;

        ApplicationDesc m_desc;
        VirtualFileSystem m_fileSystem;
        FileDialogs m_fileDialogs;
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

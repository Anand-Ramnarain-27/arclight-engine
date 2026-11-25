#pragma once

#include "Core/Time.h"

#include <string_view>

namespace arc
{
    class Application;

    /// An engine system owned and driven by the Application.
    ///
    /// init() runs in the order modules were added, and shutdown() in reverse order,
    /// so a module may depend on any module added before it. Each frame, every module
    /// runs update(), then every module runs preRender(), and so on.
    ///
    /// A frame is drawn while the next one updates: render() and postRender() run on the
    /// Renderer's render thread (or on the main thread when it is switched off), the rest on the
    /// main thread. render() must therefore not read what update() changes. Each module copies what
    /// it will draw in extract(), into storage render() is not using, and hands it over in
    /// syncRender(), the one point where the render thread is known to be idle:
    ///   main thread    update -> preRender -> extract -> [wait for the render thread] -> syncRender
    ///   render thread                                                               render -> postRender
    class Module
    {
    public:
        explicit Module(Application& app) : m_app(app) {}
        virtual ~Module() = default;

        Module(const Module&) = delete;
        Module& operator=(const Module&) = delete;

        [[nodiscard]] virtual std::string_view name() const = 0;

        /// Return false to abort start-up; log the reason before returning.
        [[nodiscard]] virtual bool init() { return true; }

        virtual void update(const FrameTime& /*time*/) {}
        virtual void preRender() {}

        /// Main thread, while the render thread may still be drawing the previous frame: copy what
        /// this frame will draw, from state that only the main thread changes.
        virtual void extract() {}

        /// Main thread, while the render thread is idle: hand what extract() built to render(), and
        /// make any change to objects render() uses. Kept short, since nothing renders meanwhile.
        virtual void syncRender() {}

        /// GPU work is recorded here, on the render thread, and only for frames that could begin
        /// (Renderer::isFrameActive()). render() is for the scene, drawn into the scene targets
        /// (Renderer::beginScenePass(), then Renderer::beginOverlayPass() for anything drawn over the
        /// tone-mapped image); postRender() is for what goes on the backbuffer, such as the UI that
        /// shows the scene.
        virtual void render() {}
        virtual void postRender() {}

        /// Called only if init() succeeded.
        virtual void shutdown() {}

    protected:
        [[nodiscard]] Application& app() const { return m_app; }

    private:
        Application& m_app;
    };
}

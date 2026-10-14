#pragma once

#include <string_view>

namespace arc
{
    class Application;

    /// An engine system owned and driven by the Application.
    ///
    /// init() runs in the order modules were added, and shutdown() in reverse order,
    /// so a module may depend on any module added before it. Each frame, every module
    /// runs update(), then every module runs preRender(), and so on.
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

        virtual void update() {}
        virtual void preRender() {}
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

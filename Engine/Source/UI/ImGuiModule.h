#pragma once

#include "Application/Module.h"
#include "Platform/PlatformEvents.h"

#include <memory>

namespace arc
{
    class ImGuiRenderBackend;

    /// Owns the Dear ImGui context and its platform and renderer backends.
    ///
    /// Must be added before any module that draws ImGui. The frame begins in update(), so later
    /// modules build their UI in update() or preRender(). The UI is drawn in postRender(), on top
    /// of whatever the scene drew to the backbuffer.
    /// Docking is enabled; the dockspace itself belongs to whichever module owns the layout.
    class ImGuiModule final : public Module, public PlatformEventListener
    {
    public:
        explicit ImGuiModule(Application& app);
        ~ImGuiModule() override;

        [[nodiscard]] std::string_view name() const override { return "ImGui"; }

        [[nodiscard]] bool init() override;
        void update(const FrameTime& time) override;
        void postRender() override;
        /// Also used to clean up after a partial init() failure, so each step checks what exists.
        void shutdown() override;

        void onPlatformEvent(const SDL_Event& event) override;

    private:
        std::unique_ptr<ImGuiRenderBackend> m_renderBackend;
        bool m_frameStarted = false;
        bool m_platformBackendReady = false;
        bool m_rendererBackendReady = false;
    };
}

#pragma once

#include "Application/Module.h"
#include "Platform/PlatformEvents.h"

#include <string>

struct SDL_Renderer;

namespace arc
{
    /// Owns the Dear ImGui context and its platform and renderer backends.
    ///
    /// Must be added before any module that draws ImGui. The frame begins in update(), so later
    /// modules build their UI in update() or preRender(); the frame is drawn and presented in postRender().
    /// Docking is enabled; the dockspace itself belongs to whichever module owns the layout.
    ///
    /// Drawing currently goes through SDL_Renderer so the editor works before the engine has its
    /// own renderer. TODO: Replace with D3D12 and Metal ImGui backends once the RHI exists (Phase 1).
    class ImGuiModule final : public Module, public PlatformEventListener
    {
    public:
        using Module::Module;

        [[nodiscard]] std::string_view name() const override { return "ImGui"; }

        [[nodiscard]] bool init() override;
        void update(const FrameTime& time) override;
        void postRender() override;
        /// Also used to clean up after a partial init() failure, so each step checks what exists.
        void shutdown() override;

        void onPlatformEvent(const SDL_Event& event) override;

        void setVSync(bool enabled);
        [[nodiscard]] bool vsync() const { return m_vsync; }

        /// Name of the graphics API SDL_Renderer chose, e.g. "direct3d11" or "metal".
        [[nodiscard]] const std::string& rendererName() const { return m_rendererName; }

    private:
        SDL_Renderer* m_renderer = nullptr;
        std::string m_rendererName;
        bool m_vsync = true;
        bool m_frameStarted = false;
        bool m_platformBackendReady = false;
        bool m_rendererBackendReady = false;
    };
}

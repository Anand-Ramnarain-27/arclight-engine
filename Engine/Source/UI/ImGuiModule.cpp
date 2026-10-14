#include "UI/ImGuiModule.h"

#include "Application/Application.h"
#include "Core/Log.h"

#include <SDL3/SDL.h>
#include <imgui.h>
#include <imgui_impl_sdl3.h>
#include <imgui_impl_sdlrenderer3.h>

namespace arc
{
    bool ImGuiModule::init()
    {
        SDL_Window* window = app().window().sdlWindow();

        m_renderer = SDL_CreateRenderer(window, nullptr);
        if (!m_renderer)
        {
            ARC_LOG_ERROR("SDL_CreateRenderer failed: {}", SDL_GetError());
            return false;
        }
        const char* rendererName = SDL_GetRendererName(m_renderer);
        m_rendererName = rendererName ? rendererName : "unknown";
        setVSync(m_vsync);

        IMGUI_CHECKVERSION();
        ImGui::CreateContext();

        ImGuiIO& io = ImGui::GetIO();
        // Keyboard navigation is left off: with it on, any focused panel captures the keyboard
        // and editor shortcuts such as F5 would stop working.
        io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;

        ImGui::StyleColorsDark();

        // Content scale is the OS UI scale (e.g. 150%), applied on top of the pixel density
        // that ImGui already handles through DisplayFramebufferScale.
        const float contentScale = SDL_GetDisplayContentScale(SDL_GetDisplayForWindow(window));
        const float scale = contentScale > 0.0f ? contentScale : 1.0f;
        ImGuiStyle& style = ImGui::GetStyle();
        style.ScaleAllSizes(scale);
        style.FontScaleDpi = scale;

        // The vector font stays sharp at any scale; the default bitmap font is designed for 13 px only.
        io.Fonts->AddFontDefaultVector();

        m_platformBackendReady = ImGui_ImplSDL3_InitForSDLRenderer(window, m_renderer);
        m_rendererBackendReady = m_platformBackendReady && ImGui_ImplSDLRenderer3_Init(m_renderer);
        if (!m_rendererBackendReady)
        {
            ARC_LOG_ERROR("Failed to initialise the ImGui SDL3 backends");
            shutdown();
            return false;
        }

        app().addEventListener(*this);
        ARC_LOG_INFO("ImGui {} initialised (renderer: {}, scale {:.2f})", IMGUI_VERSION, m_rendererName, scale);
        return true;
    }

    void ImGuiModule::update(const FrameTime& /*time*/)
    {
        ImGui_ImplSDLRenderer3_NewFrame();
        ImGui_ImplSDL3_NewFrame();
        ImGui::NewFrame();
        m_frameStarted = true;

        const ImGuiIO& io = ImGui::GetIO();
        app().input().setCaptured(io.WantCaptureKeyboard, io.WantCaptureMouse);
    }

    void ImGuiModule::postRender()
    {
        if (!m_frameStarted)
            return;
        m_frameStarted = false;

        ImGui::Render();

        const ImGuiIO& io = ImGui::GetIO();
        SDL_SetRenderScale(m_renderer, io.DisplayFramebufferScale.x, io.DisplayFramebufferScale.y);
        SDL_SetRenderDrawColorFloat(m_renderer, 0.08f, 0.09f, 0.11f, 1.0f);
        SDL_RenderClear(m_renderer);
        ImGui_ImplSDLRenderer3_RenderDrawData(ImGui::GetDrawData(), m_renderer);
        SDL_RenderPresent(m_renderer);
    }

    void ImGuiModule::shutdown()
    {
        app().removeEventListener(*this);
        app().input().setCaptured(false, false);

        if (m_rendererBackendReady)
            ImGui_ImplSDLRenderer3_Shutdown();
        if (m_platformBackendReady)
            ImGui_ImplSDL3_Shutdown();
        m_rendererBackendReady = false;
        m_platformBackendReady = false;

        if (ImGui::GetCurrentContext())
            ImGui::DestroyContext();

        if (m_renderer)
        {
            SDL_DestroyRenderer(m_renderer);
            m_renderer = nullptr;
        }
    }

    void ImGuiModule::onPlatformEvent(const SDL_Event& event)
    {
        ImGui_ImplSDL3_ProcessEvent(&event);
    }

    void ImGuiModule::setVSync(bool enabled)
    {
        m_vsync = enabled;
        if (m_renderer && !SDL_SetRenderVSync(m_renderer, enabled ? 1 : SDL_RENDERER_VSYNC_DISABLED))
            ARC_LOG_WARN("SDL_SetRenderVSync failed: {}", SDL_GetError());
    }
}

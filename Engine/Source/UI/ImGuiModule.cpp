#include "UI/ImGuiModule.h"

#include "Application/Application.h"
#include "Core/Log.h"
#include "RHI/Device.h"
#include "UI/ImGuiRenderBackend.h"

#include <SDL3/SDL.h>
#include <imgui.h>
#include <imgui_impl_sdl3.h>

namespace arc
{
    namespace
    {
        bool initPlatformBackend(SDL_Window* window, rhi::Backend backend)
        {
            switch (backend)
            {
                case rhi::Backend::D3D12: return ImGui_ImplSDL3_InitForD3D(window);
                case rhi::Backend::Metal: return ImGui_ImplSDL3_InitForMetal(window);
                case rhi::Backend::Null:  return ImGui_ImplSDL3_InitForOther(window);
            }
            return false;
        }
    }

    ImGuiModule::ImGuiModule(Application& app) : Module(app) {}

    // Defined here, where ImGuiRenderBackend is complete, for the unique_ptr destructor.
    ImGuiModule::~ImGuiModule() = default;

    bool ImGuiModule::init()
    {
        SDL_Window* window = app().window().sdlWindow();
        Renderer& renderer = app().renderer();

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

        m_platformBackendReady = initPlatformBackend(window, renderer.device().adapterInfo().backend);
        if (m_platformBackendReady)
        {
            m_renderBackend = createImGuiRenderBackend(renderer);
            m_rendererBackendReady = m_renderBackend->init();
        }
        if (!m_rendererBackendReady)
        {
            ARC_LOG_ERROR("Failed to initialise the ImGui backends");
            shutdown();
            return false;
        }

        app().addEventListener(*this);
        ARC_LOG_INFO("ImGui {} initialised (renderer backend: {}, scale {:.2f})", IMGUI_VERSION,
                     io.BackendRendererName ? io.BackendRendererName : "unknown", scale);
        return true;
    }

    void ImGuiModule::update(const FrameTime& /*time*/)
    {
        m_renderBackend->newFrame();
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

        // Always called, even without a frame to draw into, so every NewFrame() is matched.
        ImGui::Render();

        Renderer& renderer = app().renderer();
        if (!renderer.isFrameActive() || !renderer.beginBackbufferPass("ImGui"))
            return;

        m_renderBackend->render(renderer.commandList(), ImGui::GetDrawData());
        renderer.endBackbufferPass();
    }

    void ImGuiModule::shutdown()
    {
        app().removeEventListener(*this);
        app().input().setCaptured(false, false);

        if (m_rendererBackendReady)
            m_renderBackend->shutdown();
        m_renderBackend.reset();
        if (m_platformBackendReady)
            ImGui_ImplSDL3_Shutdown();
        m_rendererBackendReady = false;
        m_platformBackendReady = false;

        if (ImGui::GetCurrentContext())
            ImGui::DestroyContext();
    }

    void ImGuiModule::onPlatformEvent(const SDL_Event& event)
    {
        ImGui_ImplSDL3_ProcessEvent(&event);
    }
}

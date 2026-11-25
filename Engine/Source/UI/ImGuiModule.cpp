#include "UI/ImGuiModule.h"

#include "Application/Application.h"
#include "Core/Log.h"
#include "Core/Profiler.h"
#include "RHI/Device.h"
#include "UI/ImGuiRenderBackend.h"

#include <SDL3/SDL.h>
#include <imgui.h>
#include <imgui_impl_sdl3.h>

#include <cstring>
#include <utility>
#include <vector>

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

    // ImDrawData points into ImGui's own draw lists, which the next NewFrame() starts filling again.
    // This copy has lists of its own, kept from frame to frame so their memory is reused.
    struct ImGuiModule::DrawDataCopy
    {
        DrawDataCopy() = default;
        DrawDataCopy(const DrawDataCopy&) = delete;
        DrawDataCopy& operator=(const DrawDataCopy&) = delete;

        ~DrawDataCopy()
        {
            for (ImDrawList* list : lists)
                IM_DELETE(list);
        }

        void copy(const ImDrawData& source)
        {
            data.Clear();
            while (lists.size() < static_cast<size_t>(source.CmdLists.Size))
                lists.push_back(IM_NEW(ImDrawList)(nullptr));
            for (int i = 0; i < source.CmdLists.Size; ++i)
            {
                const ImDrawList& from = *source.CmdLists[i];
                ImDrawList& to = *lists[static_cast<size_t>(i)];
                copyVector(to.CmdBuffer, from.CmdBuffer);
                copyVector(to.IdxBuffer, from.IdxBuffer);
                copyVector(to.VtxBuffer, from.VtxBuffer);
                to.Flags = from.Flags;
                data.CmdLists.push_back(&to);
            }
            data.Valid = source.Valid;
            data.FrameCount = source.FrameCount;
            data.TotalIdxCount = source.TotalIdxCount;
            data.TotalVtxCount = source.TotalVtxCount;
            data.DisplayPos = source.DisplayPos;
            data.DisplaySize = source.DisplaySize;
            data.FramebufferScale = source.FramebufferScale;
            data.OwnerViewport = source.OwnerViewport;
            // Texture updates happen in syncRender(), on the main thread, never while drawing.
            data.Textures = nullptr;
            pending = true;
        }

        // A command may name a texture by its ImTextureData, whose ID is only known once the texture
        // has been uploaded and which the main thread keeps changing; the ID itself is all the
        // render thread needs.
        void resolveTextureIds()
        {
            for (ImDrawList* list : data.CmdLists)
                for (ImDrawCmd& command : list->CmdBuffer)
                    if (command.TexRef._TexData)
                        command.TexRef = ImTextureRef(command.TexRef.GetTexID());
        }

        template <typename T>
        static void copyVector(ImVector<T>& to, const ImVector<T>& from)
        {
            // resize() keeps the capacity, where ImVector's assignment frees it first.
            to.resize(from.Size);
            if (from.Size > 0)
                std::memcpy(to.Data, from.Data, from.size_in_bytes());
        }

        ImDrawData data;
        std::vector<ImDrawList*> lists;
        bool pending = false;   // Copied and not drawn yet.
    };

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
        ARC_PROFILE_SCOPE("ImGui new frame");
        m_renderBackend->newFrame();
        ImGui_ImplSDL3_NewFrame();
        ImGui::NewFrame();
        m_frameStarted = true;

        const ImGuiIO& io = ImGui::GetIO();
        app().input().setCaptured(io.WantCaptureKeyboard, io.WantCaptureMouse);
    }

    void ImGuiModule::extract()
    {
        if (!m_frameStarted)
            return;
        m_frameStarted = false;
        {
            ARC_PROFILE_SCOPE("ImGui render");
            ImGui::Render();
        }
        ARC_PROFILE_SCOPE("Copy ImGui draw data");
        if (!m_next)
            m_next = std::make_unique<DrawDataCopy>();
        m_next->copy(*ImGui::GetDrawData());
    }

    void ImGuiModule::syncRender()
    {
        for (ImTextureData* texture : ImGui::GetPlatformIO().Textures)
            if (texture->Status != ImTextureStatus_OK)
                m_renderBackend->updateTexture(*texture);
        if (!m_next || !m_next->pending)
            return;
        m_next->resolveTextureIds();
        std::swap(m_next, m_drawing);
        if (m_next)
            m_next->pending = false;
    }

    void ImGuiModule::postRender()
    {
        ARC_PROFILE_SCOPE("Record ImGui");
        if (!m_drawing || !m_drawing->pending)
            return;
        m_drawing->pending = false;
        Renderer& renderer = app().renderer();
        if (!renderer.beginBackbufferPass("ImGui"))
            return;
        m_renderBackend->render(renderer.commandList(), &m_drawing->data);
        renderer.endBackbufferPass();
    }

    void ImGuiModule::shutdown()
    {
        app().removeEventListener(*this);
        app().input().setCaptured(false, false);

        m_next.reset();
        m_drawing.reset();
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

    uint64_t ImGuiModule::textureId(rhi::Texture& texture)
    {
        return m_renderBackend->textureId(texture);
    }

    void ImGuiModule::onPlatformEvent(const SDL_Event& event)
    {
        ImGui_ImplSDL3_ProcessEvent(&event);
    }
}

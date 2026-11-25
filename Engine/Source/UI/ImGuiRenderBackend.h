#pragma once

#include <cstdint>
#include <memory>

struct ImDrawData;
struct ImTextureData;

namespace arc
{
    class Renderer;

    namespace rhi
    {
        class CommandList;
        class Texture;
    }

    /// Bridges Dear ImGui's renderer backend for the active RHI backend (imgui_impl_dx12 or
    /// imgui_impl_metal) to the RHI. Each implementation reaches into its backend's native
    /// objects, which keeps ImGui out of the RHI itself.
    class ImGuiRenderBackend
    {
    public:
        virtual ~ImGuiRenderBackend() = default;

        /// The ImGui context must exist. Returns false and logs on failure.
        [[nodiscard]] virtual bool init() = 0;
        virtual void shutdown() = 0;

        /// Called before ImGui::NewFrame(), on the main thread.
        virtual void newFrame() = 0;
        /// Creates, updates or destroys the texture on the GPU as its status asks, and sets the new
        /// status. On the main thread, while no frame is being recorded.
        virtual void updateTexture(ImTextureData& texture) = 0;
        /// Records the draw data inside the current render pass, on the thread recording frames. Its
        /// textures are up to date and its commands name them by ImTextureID; it lists no textures to
        /// update.
        virtual void render(rhi::CommandList& commandList, ImDrawData* drawData) = 0;
        /// The ImTextureID that shows `texture` in ImGui::Image() this frame, on the main thread. The
        /// texture must have been created by the device (it has a handle) with Sampled usage, and be
        /// in the ShaderRead state when the UI is drawn.
        [[nodiscard]] virtual uint64_t textureId(rhi::Texture& texture) = 0;
    };

    /// Defined once per platform, for that platform's RHI backend.
    [[nodiscard]] std::unique_ptr<ImGuiRenderBackend> createImGuiRenderBackend(Renderer& renderer);
}

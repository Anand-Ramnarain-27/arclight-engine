#include "UI/ImGuiRenderBackend.h"

#include <imgui.h>

namespace arc
{
    namespace
    {
        // Discards the draw data. Texture requests are still acknowledged, because ImGui keeps
        // asking to create the font atlas until a backend marks it as created.
        class NullImGuiRenderBackend final : public ImGuiRenderBackend
        {
        public:
            bool init() override
            {
                ImGuiIO& io = ImGui::GetIO();
                io.BackendRendererName = "arclight_null";
                io.BackendFlags |= ImGuiBackendFlags_RendererHasTextures;
                return true;
            }

            void shutdown() override
            {
                for (ImTextureData* texture : ImGui::GetPlatformIO().Textures)
                    if (texture->RefCount == 1)
                        destroyTexture(texture);

                ImGuiIO& io = ImGui::GetIO();
                io.BackendRendererName = nullptr;
                io.BackendFlags &= ~ImGuiBackendFlags_RendererHasTextures;
            }

            void newFrame() override {}

            void render(rhi::CommandList& /*commandList*/, ImDrawData* drawData) override
            {
                if (!drawData->Textures)
                    return;
                for (ImTextureData* texture : *drawData->Textures)
                {
                    if (texture->Status == ImTextureStatus_WantCreate || texture->Status == ImTextureStatus_WantUpdates)
                    {
                        texture->SetTexID(static_cast<ImTextureID>(1));
                        texture->SetStatus(ImTextureStatus_OK);
                    }
                    else if (texture->Status == ImTextureStatus_WantDestroy && texture->UnusedFrames > 0)
                    {
                        destroyTexture(texture);
                    }
                }
            }

        private:
            static void destroyTexture(ImTextureData* texture)
            {
                texture->SetTexID(ImTextureID_Invalid);
                texture->SetStatus(ImTextureStatus_Destroyed);
            }
        };
    }

    std::unique_ptr<ImGuiRenderBackend> createImGuiRenderBackend(Renderer& /*renderer*/)
    {
        return std::make_unique<NullImGuiRenderBackend>();
    }
}

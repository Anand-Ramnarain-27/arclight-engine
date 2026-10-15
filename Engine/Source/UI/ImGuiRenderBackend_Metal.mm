#include "UI/ImGuiRenderBackend.h"

#include "Core/Assert.h"
#include "RHI/Metal/MetalBackend.h"
#include "Renderer/Renderer.h"

#include <imgui.h>
#include <imgui_impl_metal.h>

namespace arc
{
    namespace
    {
        class MetalImGuiRenderBackend final : public ImGuiRenderBackend
        {
        public:
            explicit MetalImGuiRenderBackend(Renderer& renderer)
                : m_device(static_cast<rhi::metal::MetalDevice&>(renderer.device()))
            {
            }

            bool init() override { return ImGui_ImplMetal_Init(m_device.native()); }
            void shutdown() override { ImGui_ImplMetal_Shutdown(); }

            // ImGui_ImplMetal_NewFrame reads the pixel format from the render pass's attachment
            // texture, which is the drawable and is only known once the pass has begun, so it
            // is called from render() instead.
            void newFrame() override {}

            void render(rhi::CommandList& commandList, ImDrawData* drawData) override
            {
                auto& metalCommandList = static_cast<rhi::metal::MetalCommandList&>(commandList);
                ImGui_ImplMetal_NewFrame(metalCommandList.renderPassDescriptor());
                ImGui_ImplMetal_RenderDrawData(drawData, metalCommandList.commandBuffer(),
                                               metalCommandList.renderEncoder());
            }

            uint64_t textureId(rhi::Texture& texture) override
            {
                ARC_ASSERT(!texture.handle().isNull(), "Only textures created by the device can be shown in ImGui");
                // imgui_impl_metal reads the ID back as an id<MTLTexture>. The ID does not keep the
                // texture alive; the command buffer that draws the UI retains it once encoded.
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wold-style-cast"
                void* pointer = (__bridge void*)static_cast<rhi::metal::MetalTexture&>(texture).native();
#pragma clang diagnostic pop
                return reinterpret_cast<uintptr_t>(pointer);
            }

        private:
            rhi::metal::MetalDevice& m_device;
        };
    }

    std::unique_ptr<ImGuiRenderBackend> createImGuiRenderBackend(Renderer& renderer)
    {
        return std::make_unique<MetalImGuiRenderBackend>(renderer);
    }
}

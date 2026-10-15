#include "Panels/ScenePanel.h"

#include "Application/Application.h"
#include "RHI/Device.h"
#include "RHI/Texture.h"
#include "UI/ImGuiModule.h"

#include <imgui.h>

#include <algorithm>
#include <cmath>

namespace arc
{
    void ScenePanel::draw(bool* open)
    {
        m_hovered = false;

        ImGui::SetNextWindowSize({960.0f, 540.0f}, ImGuiCond_FirstUseEver);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {0.0f, 0.0f});
        const bool visible = ImGui::Begin(title(), open, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
        ImGui::PopStyleVar();
        if (!visible)
        {
            ImGui::End();
            return;
        }

        // ImGui lays out in points; the target is sized in pixels so a Retina display gets full
        // resolution rather than an upscaled image.
        const ImVec2 available = ImGui::GetContentRegionAvail();
        const ImVec2 scale = ImGui::GetIO().DisplayFramebufferScale;
        const Extent2D pixels{static_cast<uint32_t>(std::max(1.0f, std::floor(available.x * scale.x))),
                              static_cast<uint32_t>(std::max(1.0f, std::floor(available.y * scale.y)))};

        // Resized before its ImTextureID is taken, so this frame's UI shows the new target, which
        // the scene is rendered into later in the same frame.
        Renderer& renderer = m_app.renderer();
        renderer.setSceneSize(pixels);

        if (rhi::Texture* scene = renderer.sceneTexture())
        {
            const ImVec2 origin = ImGui::GetCursorPos();
            ImGui::Image(ImTextureRef(m_imgui.textureId(*scene)), available);
            m_hovered = ImGui::IsItemHovered();

            const Extent2D size = scene->desc().size;
            ImGui::SetCursorPos({origin.x + 8.0f, origin.y + 6.0f});
            ImGui::TextDisabled("%u x %u px  %s", size.width, size.height,
                                rhi::backendName(renderer.device().adapterInfo().backend));
        }

        ImGui::End();
    }
}

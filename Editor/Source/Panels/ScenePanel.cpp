#include "Panels/ScenePanel.h"

#include "Application/Application.h"
#include "RHI/Device.h"
#include "RHI/Texture.h"
#include "Scene/EditorCamera.h"
#include "Scene/EditorGrid.h"
#include "UI/ImGuiModule.h"

#include <imgui.h>

#include <algorithm>
#include <array>
#include <cmath>

namespace arc
{
    namespace
    {
        constexpr const char* kControlsHelp =
            "Right drag: look around\n"
            "Right + W A S D: fly, Q / E: down / up, Shift: faster\n"
            "Right + wheel: fly speed\n"
            "Alt + left drag: orbit\n"
            "Middle drag or Alt + Shift + left drag: pan\n"
            "Wheel: dolly\n"
            "F: focus on the origin";

        const char* modeName(EditorCamera::Mode mode)
        {
            switch (mode)
            {
                case EditorCamera::Mode::Fly:   return "Fly";
                case EditorCamera::Mode::Orbit: return "Orbit";
                case EditorCamera::Mode::Pan:   return "Pan";
                case EditorCamera::Mode::Idle:  return "";
            }
            return "";
        }

        ImVec2 operator+(ImVec2 a, ImVec2 b) { return {a.x + b.x, a.y + b.y}; }
        ImVec2 operator-(ImVec2 a, ImVec2 b) { return {a.x - b.x, a.y - b.y}; }
    }

    void ScenePanel::draw(bool* open)
    {
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
        const ImVec2 size{std::max(available.x, 1.0f), std::max(available.y, 1.0f)};
        const ImVec2 scale = ImGui::GetIO().DisplayFramebufferScale;
        const Extent2D pixels{static_cast<uint32_t>(std::max(1.0f, std::floor(size.x * scale.x))),
                              static_cast<uint32_t>(std::max(1.0f, std::floor(size.y * scale.y)))};

        // Resized before its ImTextureID is taken, so this frame's UI shows the new target, which
        // the scene is rendered into later in the same frame.
        Renderer& renderer = m_app.renderer();
        renderer.setSceneSize(pixels);

        // An invisible button over the whole image rather than the image alone: a button becomes the
        // active item when pressed and stays active until released, so a drag that leaves the panel
        // keeps steering the camera, and dragging never moves a floating window. The overlays drawn
        // after it take the mouse where they overlap it.
        const ImVec2 origin = ImGui::GetCursorScreenPos();
        ImGui::SetNextItemAllowOverlap();
        ImGui::InvisibleButton("##Viewport", size,
                               ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight |
                                   ImGuiButtonFlags_MouseButtonMiddle);
        const bool ownsInput = ImGui::IsItemHovered() || ImGui::IsItemActive();

        if (rhi::Texture* scene = renderer.sceneTexture())
            ImGui::GetWindowDrawList()->AddImage(ImTextureRef(m_imgui.textureId(*scene)), origin, origin + size);

        // ImGui reports the mouse as wanted whenever it is over any window, this one included, so
        // Input would hide it from the camera. Over the viewport it belongs to the scene; the
        // keyboard too, unless a text field is being edited.
        if (ownsInput)
        {
            Input& input = m_app.input();
            input.setCaptured(ImGui::GetIO().WantTextInput, false);
            // Input measures the mouse in pixels, so the viewport height is passed in pixels too.
            m_camera.update(input, m_app.frameTime().realDelta, static_cast<float>(pixels.height));
        }

        ImGui::SetCursorScreenPos(origin + ImVec2{8.0f, 6.0f});
        const Extent2D targetSize = renderer.sceneSize();
        ImGui::TextDisabled("%u x %u px  %s", targetSize.width, targetSize.height,
                            rhi::backendName(renderer.device().adapterInfo().backend));

        ImGui::SetCursorScreenPos(origin + ImVec2{8.0f, 8.0f + ImGui::GetTextLineHeightWithSpacing()});
        if (ImGui::SmallButton("Camera"))
            ImGui::OpenPopup("Scene Settings");
        ImGui::SameLine();
        ImGui::TextDisabled("(?)");
        ImGui::SetItemTooltip("%s", kControlsHelp);
        if (ownsInput && m_camera.mode() != EditorCamera::Mode::Idle)
        {
            ImGui::SameLine();
            ImGui::TextUnformatted(modeName(m_camera.mode()));
        }
        drawSettings();

        drawAxisGizmo(origin, origin + size);
        ImGui::End();
    }

    void ScenePanel::drawSettings()
    {
        if (!ImGui::BeginPopup("Scene Settings"))
            return;

        Camera& camera = m_camera.camera();
        ImGui::SeparatorText("Camera");
        float fovDegrees = toDegrees(camera.verticalFov);
        if (ImGui::SliderFloat("Field of view", &fovDegrees, 20.0f, 120.0f, "%.0f deg"))
            camera.verticalFov = toRadians(fovDegrees);
        ImGui::SliderFloat("Near plane", &camera.nearZ, 0.001f, 10.0f, "%.3f", ImGuiSliderFlags_Logarithmic);
        ImGui::SetItemTooltip("Distance to the near clip plane. Depth precision depends far more on this than on "
                              "the far plane: try 0.001 with Reversed-Z off.");
        ImGui::Checkbox("Reversed-Z", &camera.reversedZ);
        ImGui::SetItemTooltip("Near plane at depth 1 and infinity at 0, with a GreaterEqual depth test. Spreads "
                              "the float depth buffer's precision evenly over distance.");
        ImGui::BeginDisabled(camera.reversedZ);
        ImGui::SliderFloat("Far plane", &camera.farZ, 10.0f, 100000.0f, "%.0f", ImGuiSliderFlags_Logarithmic);
        ImGui::EndDisabled();
        if (camera.reversedZ)
            ImGui::SetItemTooltip("The reversed projection has no far plane: it reaches infinity.");
        camera.farZ = std::max(camera.farZ, camera.nearZ * 2.0f);
        ImGui::SliderFloat("Fly speed", &m_camera.flySpeed, 0.1f, 500.0f, "%.1f /s", ImGuiSliderFlags_Logarithmic);
        if (ImGui::Button("Reset view"))
            m_camera.reset();

        const Vec3 position = camera.position;
        ImGui::TextDisabled("Position (%.2f, %.2f, %.2f)", position.x, position.y, position.z);
        ImGui::TextDisabled("Yaw %.1f, pitch %.1f deg, focus %.2f away", toDegrees(camera.yaw),
                            toDegrees(camera.pitch), m_camera.focusDistance());

        ImGui::SeparatorText("Grid");
        ImGui::Checkbox("Show grid", &m_grid.visible);
        ImGui::SliderFloat("Cell size", &m_grid.cellSize, 0.1f, 10.0f, "%.2f", ImGuiSliderFlags_Logarithmic);
        ImGui::SliderFloat("Major every", &m_grid.majorEvery, 2.0f, 20.0f, "%.0f cells");
        m_grid.majorEvery = std::round(m_grid.majorEvery);
        ImGui::SliderFloat("Fade distance", &m_grid.fadeDistance, 10.0f, 1000.0f, "%.0f",
                           ImGuiSliderFlags_Logarithmic);
        ImGui::SliderFloat("Opacity", &m_grid.opacity, 0.0f, 1.0f, "%.2f");

        ImGui::EndPopup();
    }

    void ScenePanel::drawAxisGizmo(ImVec2 viewMin, ImVec2 viewMax)
    {
        constexpr float kRadius = 38.0f;   // Points from the centre to an axis end.
        constexpr float kHandle = 9.0f;    // Radius of the circle at each axis end.
        constexpr float kMargin = 12.0f;

        const ImVec2 center{viewMax.x - kRadius - kHandle - kMargin, viewMin.y + kRadius + kHandle + kMargin};
        if (center.x - kRadius < viewMin.x || center.y + kRadius > viewMax.y)
            return;

        struct AxisEnd
        {
            Vec3 axis;
            ImU32 color;
            const char* label;
            const char* tooltip;
            Vec3 inView{};
        };
        std::array<AxisEnd, 6> ends = {{
            {{1.0f, 0.0f, 0.0f}, IM_COL32(230, 70, 70, 255), "X", "View from +X"},
            {{0.0f, 1.0f, 0.0f}, IM_COL32(120, 200, 70, 255), "Y", "View from +Y (top)"},
            {{0.0f, 0.0f, 1.0f}, IM_COL32(70, 120, 240, 255), "Z", "View from +Z (front)"},
            {{-1.0f, 0.0f, 0.0f}, IM_COL32(230, 70, 70, 255), nullptr, "View from -X"},
            {{0.0f, -1.0f, 0.0f}, IM_COL32(120, 200, 70, 255), nullptr, "View from -Y (bottom)"},
            {{0.0f, 0.0f, -1.0f}, IM_COL32(70, 120, 240, 255), nullptr, "View from -Z (back)"},
        }};

        // The axes as the camera sees them: x right, y up, z toward the viewer. Drawn back to front
        // so the ends pointing at the viewer are on top.
        const Mat4 view = m_camera.camera().view();
        for (AxisEnd& end : ends)
            end.inView = view.transformVector(end.axis);
        std::sort(ends.begin(), ends.end(), [](const AxisEnd& a, const AxisEnd& b) { return a.inView.z < b.inView.z; });

        ImDrawList* drawList = ImGui::GetWindowDrawList();
        drawList->AddCircleFilled(center, kRadius + kHandle + 2.0f, IM_COL32(20, 22, 26, 120));

        for (const AxisEnd& end : ends)
        {
            const ImVec2 position = center + ImVec2{end.inView.x * kRadius, -end.inView.y * kRadius};

            ImGui::SetCursorScreenPos(position - ImVec2{kHandle, kHandle});
            ImGui::PushID(end.tooltip);
            ImGui::InvisibleButton("##Axis", {kHandle * 2.0f, kHandle * 2.0f});
            const bool hovered = ImGui::IsItemHovered();
            if (ImGui::IsItemClicked(ImGuiMouseButton_Left))
                m_camera.viewAlong(-end.axis);
            ImGui::SetItemTooltip("%s", end.tooltip);
            ImGui::PopID();

            const ImU32 fill = hovered ? IM_COL32(255, 255, 255, 255) : end.color;
            if (end.label)
            {
                drawList->AddLine(center, position, end.color, 2.0f);
                drawList->AddCircleFilled(position, kHandle, fill);
                const ImVec2 textSize = ImGui::CalcTextSize(end.label);
                drawList->AddText(position - ImVec2{textSize.x * 0.5f, textSize.y * 0.5f}, IM_COL32(15, 15, 18, 255),
                                  end.label);
            }
            else
            {
                drawList->AddCircleFilled(position, kHandle * 0.8f, hovered ? fill : IM_COL32(30, 32, 38, 230));
                drawList->AddCircle(position, kHandle * 0.8f, end.color, 0, 1.5f);
            }
        }
    }
}

#include "Panels/ScenePanel.h"

#include "Application/Application.h"
#include "EditorSelection.h"
#include "RHI/Device.h"
#include "RHI/Texture.h"
#include "Scene/EditorCamera.h"
#include "Scene/EditorGrid.h"
#include "Scene/MeshComponent.h"
#include "Scene/Raycast.h"
#include "Scene/SceneModule.h"
#include "Scene/SelectionOutline.h"
#include "UI/ImGuiModule.h"

#include <imgui.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <optional>

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
            "F: frame the selection, or focus on the origin\n"
            "Left click: select\n"
            "W / E / R: move / rotate / scale gizmo; hold Ctrl to toggle snapping";

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
        //
        // While a gizmo handle is under the pointer or being dragged the button is left out:
        // ImGuizmo does not start a drag while another item is hovered.
        const ImVec2 origin = ImGui::GetCursorScreenPos();
        GameObject* selected = m_selection.object();
        const bool gizmoActive = selected && TransformGizmo::isActive();
        bool ownsInput = false;
        bool clicked = false;
        if (gizmoActive)
        {
            ImGui::Dummy(size);
        }
        else
        {
            ImGui::SetNextItemAllowOverlap();
            ImGui::InvisibleButton("##Viewport", size,
                                   ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight |
                                       ImGuiButtonFlags_MouseButtonMiddle);
            ownsInput = ImGui::IsItemHovered() || ImGui::IsItemActive();
            clicked = ImGui::IsItemClicked(ImGuiMouseButton_Left);
        }

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
            if (input.wasPressed(Key::F))
            {
                if (selected)
                    m_camera.frame(hierarchyBounds(*selected), selected->transform().worldPosition());
                else
                    m_camera.focusOnOrigin();
            }
            handleGizmoShortcuts();
        }

        // Alt + left drag orbits; only a plain click selects.
        if (clicked && !ImGui::GetIO().KeyAlt)
            pick(ImGui::GetIO().MouseClickedPos[ImGuiMouseButton_Left], origin, size);

        // After the image, so the handles are drawn over it, and before the overlays, which are
        // drawn over the handles. An object under an inactive parent is not drawn, so it gets none.
        selected = m_selection.object();
        if (selected && selected->isActiveInHierarchy())
            m_gizmo.draw(*selected, m_camera.camera(), origin, size);

        ImGui::SetCursorScreenPos(origin + ImVec2{8.0f, 6.0f});
        const Extent2D targetSize = renderer.sceneSize();
        ImGui::TextDisabled("%u x %u px  %s", targetSize.width, targetSize.height,
                            rhi::backendName(renderer.device().adapterInfo().backend));

        ImGui::SetCursorScreenPos(origin + ImVec2{8.0f, 8.0f + ImGui::GetTextLineHeightWithSpacing()});
        if (ImGui::SmallButton("View"))
            ImGui::OpenPopup("Scene Settings");
        ImGui::SameLine();
        ImGui::TextDisabled("(?)");
        ImGui::SetItemTooltip("%s", kControlsHelp);
        ImGui::SameLine(0.0f, 16.0f);
        drawGizmoToolbar();
        if (ownsInput && m_camera.mode() != EditorCamera::Mode::Idle)
        {
            ImGui::SameLine(0.0f, 16.0f);
            ImGui::TextUnformatted(modeName(m_camera.mode()));
        }
        drawSettings();

        drawAxisGizmo(origin, origin + size);
        ImGui::End();
    }

    void ScenePanel::pick(ImVec2 mouse, ImVec2 viewMin, ImVec2 viewSize)
    {
        // Normalised device coordinates: [-1, 1] across the image, +Y up, where ImGui's y grows
        // downwards. Points or pixels give the same result, as both axes are divided by their size.
        const Vec2 ndc{(mouse.x - viewMin.x) / viewSize.x * 2.0f - 1.0f, 1.0f - (mouse.y - viewMin.y) / viewSize.y * 2.0f};
        const Ray ray = m_camera.camera().viewportRay(ndc, viewSize.x / viewSize.y);
        const std::optional<RaycastHit> hit = raycast(m_scene.scene(), ray);
        m_selection.select(hit ? hit->object : nullptr);
    }

    void ScenePanel::handleGizmoShortcuts()
    {
        // W, E and R also fly the camera while the right button is held.
        const Input& input = m_app.input();
        if (ImGui::GetIO().WantTextInput || input.isDown(MouseButton::Right))
            return;
        if (ImGui::IsKeyPressed(ImGuiKey_W, false))
            m_gizmo.operation = TransformGizmo::Operation::Translate;
        else if (ImGui::IsKeyPressed(ImGuiKey_E, false))
            m_gizmo.operation = TransformGizmo::Operation::Rotate;
        else if (ImGui::IsKeyPressed(ImGuiKey_R, false))
            m_gizmo.operation = TransformGizmo::Operation::Scale;
    }

    void ScenePanel::drawGizmoToolbar()
    {
        const auto toggle = [](const char* label, bool on, const char* tooltip) {
            if (on)
                ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive));
            const bool pressed = ImGui::SmallButton(label);
            if (on)
                ImGui::PopStyleColor();
            ImGui::SetItemTooltip("%s", tooltip);
            return pressed;
        };

        using Operation = TransformGizmo::Operation;
        if (toggle("Move", m_gizmo.operation == Operation::Translate, "Move (W)"))
            m_gizmo.operation = Operation::Translate;
        ImGui::SameLine();
        if (toggle("Rotate", m_gizmo.operation == Operation::Rotate, "Rotate (E)"))
            m_gizmo.operation = Operation::Rotate;
        ImGui::SameLine();
        if (toggle("Scale", m_gizmo.operation == Operation::Scale, "Scale (R)"))
            m_gizmo.operation = Operation::Scale;

        ImGui::SameLine(0.0f, 12.0f);
        const bool local = m_gizmo.space == TransformGizmo::Space::Local;
        if (toggle(local ? "Local" : "World", false,
                   "Gizmo axes: the world's or the object's own. Scaling always uses the object's own axes."))
            m_gizmo.space = local ? TransformGizmo::Space::World : TransformGizmo::Space::Local;
        ImGui::SameLine();
        if (toggle("Snap", m_gizmo.snap, "Snap to the steps set in View > Gizmo. Hold Ctrl while dragging to toggle."))
            m_gizmo.snap = !m_gizmo.snap;
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

        ImGui::SeparatorText("Shading");
        int view = static_cast<int>(m_scene.debugView);
        if (ImGui::Combo("Shading", &view, "Lit\0Base colour\0Normals\0Texture coordinates\0"))
            m_scene.debugView = static_cast<MeshRenderer::DebugView>(view);

        ImGui::SeparatorText("Rendering");
        ImGui::Checkbox("Sort draws", &m_scene.sortDraws);
        ImGui::SetItemTooltip("Sorts opaque draws by pipeline, geometry, material and then distance, so each state "
                              "change happens once per group. Untick to see how many more changes the scene order needs.");
        const RenderStats& stats = m_scene.renderStats();
        ImGui::TextDisabled("%u draws, %u triangles", stats.draws, stats.triangles);
        ImGui::TextDisabled("%u pipeline changes, %u buffer binds", stats.pipelineChanges, stats.bufferBinds);
        ImGui::TextDisabled("%u texture binds, %u constant uploads", stats.textureBinds, stats.constantUploads);

        ImGui::SeparatorText("Gizmo");
        ImGui::SliderFloat("Move step", &m_gizmo.translateSnap, 0.01f, 10.0f, "%.2f", ImGuiSliderFlags_Logarithmic);
        ImGui::SliderFloat("Rotate step", &m_gizmo.rotateSnap, 1.0f, 90.0f, "%.0f deg");
        ImGui::SliderFloat("Scale step", &m_gizmo.scaleSnap, 0.01f, 1.0f, "%.2f", ImGuiSliderFlags_Logarithmic);

        ImGui::SeparatorText("Selection outline");
        ImGui::ColorEdit3("Colour", &m_outline.color.x, ImGuiColorEditFlags_NoInputs);
        ImGui::SliderInt("Thickness", &m_outline.thickness, 1, 4, "%d px");

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

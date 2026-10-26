#include "Panels/ScenePanel.h"

#include "Application/Application.h"
#include "Core/Log.h"
#include "EditorFiles.h"
#include "EditorSelection.h"
#include "Math/Color.h"
#include "RHI/Device.h"
#include "RHI/Texture.h"
#include "Renderer/ToneMapper.h"
#include "Scene/EditorCamera.h"
#include "Scene/EditorGrid.h"
#include "Scene/LightComponent.h"
#include "Scene/MeshComponent.h"
#include "Scene/Raycast.h"
#include "Scene/SceneModule.h"
#include "Scene/SelectionOutline.h"
#include "UI/ImGuiModule.h"

#include <imgui.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <iterator>
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

        constexpr float kLightIconRadius = 7.0f;   // Points.

        // World space to the panel's screen coordinates, in points.
        class ScreenProjection
        {
        public:
            ScreenProjection(const Camera& camera, ImVec2 viewMin, ImVec2 viewSize)
                : m_viewProjection(camera.projection(viewSize.x / viewSize.y) * camera.view())
                , m_min(viewMin)
                , m_size(viewSize)
            {
            }

            // Nothing for points behind the camera, which have no place on screen.
            [[nodiscard]] std::optional<ImVec2> operator()(const Vec3& world) const
            {
                const Vec4 clip = m_viewProjection * Vec4(world, 1.0f);
                if (clip.w <= 1e-4f)
                    return std::nullopt;
                const float x = clip.x / clip.w;
                const float y = clip.y / clip.w;
                return ImVec2{m_min.x + (x * 0.5f + 0.5f) * m_size.x, m_min.y + (0.5f - y * 0.5f) * m_size.y};
            }

            // A circle in the plane spanned by the unit vectors u and v; segments with an end behind
            // the camera are left out.
            void circle(ImDrawList* drawList, const Vec3& center, const Vec3& u, const Vec3& v, float radius,
                        ImU32 color) const
            {
                constexpr int kSegments = 48;
                std::optional<ImVec2> previous = (*this)(center + u * radius);
                for (int i = 1; i <= kSegments; ++i)
                {
                    const float angle = kTwoPi * static_cast<float>(i) / kSegments;
                    const std::optional<ImVec2> point = (*this)(center + (u * std::cos(angle) + v * std::sin(angle)) * radius);
                    if (previous && point)
                        drawList->AddLine(*previous, *point, color, 1.5f);
                    previous = point;
                }
            }

            void line(ImDrawList* drawList, const Vec3& a, const Vec3& b, ImU32 color) const
            {
                const std::optional<ImVec2> start = (*this)(a);
                const std::optional<ImVec2> end = (*this)(b);
                if (start && end)
                    drawList->AddLine(*start, *end, color, 1.5f);
            }

        private:
            Mat4 m_viewProjection;
            ImVec2 m_min;
            ImVec2 m_size;
        };

        // Two unit vectors perpendicular to `axis` (unit length) and to each other.
        void perpendicularBasis(const Vec3& axis, Vec3& u, Vec3& v)
        {
            const Vec3 helper = std::abs(axis.y) < 0.9f ? Vec3::up() : Vec3::right();
            u = normalize(cross(axis, helper));
            v = cross(axis, u);
        }

        // A light's colour at full brightness, as the icon's fill.
        ImU32 iconColor(const Vec3& linear)
        {
            const float peak = std::max({linear.x, linear.y, linear.z, 1e-6f});
            const Vec3 encoded = linearToSrgb(linear / peak);
            return IM_COL32(static_cast<int>(encoded.x * 255.0f), static_cast<int>(encoded.y * 255.0f),
                            static_cast<int>(encoded.z * 255.0f), 255);
        }
    }

    void ScenePanel::draw(bool* open)
    {
        if (m_pendingProceduralSky)
            m_scene.skybox().useProceduralSky(m_app.renderer());
        else if (!m_pendingSkyboxFolder.empty())
            m_scene.skybox().load(m_app.renderer(), m_pendingSkyboxFolder);
        m_pendingProceduralSky = false;
        m_pendingSkyboxFolder.clear();

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
        if (ImGui::BeginDragDropTarget())
        {
            if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(kAssetPathPayload); payload && onAssetDropped)
                onAssetDropped(static_cast<const char*>(payload->Data));
            ImGui::EndDragDropTarget();
        }

        if (rhi::Texture* scene = renderer.displayTexture())
            ImGui::GetWindowDrawList()->AddImage(ImTextureRef(m_imgui.textureId(*scene)), origin, origin + size);
        placeLightIcons(origin, size);
        drawLightIcons(origin, size);

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
        if (ImGui::SmallButton("Lighting"))
            ImGui::OpenPopup("Lighting Settings");
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
        drawLightingSettings();

        drawAxisGizmo(origin, origin + size);
        ImGui::End();
    }

    void ScenePanel::setSkyboxFolder(const std::string& folder)
    {
        m_pendingSkyboxFolder = folder;
        const size_t length = std::min(folder.size(), m_skyboxFolder.size() - 1);
        std::memcpy(m_skyboxFolder.data(), folder.data(), length);
        m_skyboxFolder[length] = '\0';
    }

    void ScenePanel::browseForSkybox()
    {
        const VirtualFileSystem& fileSystem = m_app.fileSystem();
        FileDialogDesc desc;
        desc.type = FileDialogType::OpenFolder;
        desc.title = "Choose a folder with px, nx, py, ny, pz and nz";
        desc.defaultLocation = fileSystem.nativePath("assets://");
        m_app.fileDialogs().open(m_app.window().sdlWindow(), std::move(desc), [this](const FileDialogResult& result) {
            if (result.status == FileDialogResult::Status::Chosen)
                setSkyboxFolder(preferredPath(m_app.fileSystem(), result.paths.front()));
            else if (result.status == FileDialogResult::Status::Failed)
                ARC_LOG_WARN("The folder dialog is not available ({}); type the folder instead", result.error);
        });
    }

    void ScenePanel::pick(ImVec2 mouse, ImVec2 viewMin, ImVec2 viewSize)
    {
        // Light icons are drawn over the scene, so they are picked first.
        if (GameObject* light = lightIconAt(mouse))
        {
            m_selection.select(light);
            return;
        }

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
        if (ImGui::Combo("Shading", &view,
                         "Lit\0Base colour\0Normals\0Texture coordinates\0Diffuse light\0Specular light\0"
                         "Lighting only\0Metallic\0Roughness\0Occlusion\0Emissive\0Tangents\0"))
            m_scene.debugView = static_cast<MeshRenderer::DebugView>(view);
        ImGui::SetItemTooltip("Base colour, normals, tangents, texture coordinates, metallic, roughness and occlusion "
                              "are data, shown without exposure or tone mapping. The lighting views and emissive are "
                              "light, and go through both.");
        ImGui::Checkbox("Light icons", &m_showLightIcons);

        ImGui::SeparatorText("Rendering");
        ImGui::Checkbox("Sort draws", &m_scene.sortDraws);
        ImGui::SetItemTooltip("Sorts opaque draws by pipeline, geometry, material and then distance, so each state "
                              "change happens once per group. Untick to see how many more changes the scene order needs.");
        const RenderStats& stats = m_scene.renderStats();
        ImGui::TextDisabled("%u draws, %u triangles", stats.draws, stats.triangles);
        ImGui::TextDisabled("%u pipeline changes, %u buffer binds", stats.pipelineChanges, stats.bufferBinds);
        ImGui::TextDisabled("%u texture binds, %u constant uploads", stats.textureBinds, stats.constantUploads);

        ImGui::SeparatorText("Visibility");
        ImGui::Checkbox("Frustum culling", &m_scene.frustumCulling);
        ImGui::SameLine();
        ImGui::Checkbox("Freeze", &m_scene.freezeCulling);
        ImGui::SetItemTooltip("Keeps culling and level-of-detail choices where the camera is now. Fly out to see "
                              "what is and is not drawn.");
        const CullingStats& culling = m_scene.cullingStats();
        ImGui::TextDisabled("%u of %u meshes culled", culling.meshesCulled, culling.meshes);
        ImGui::TextDisabled("%u objects tested, %u subtrees culled whole, %u meshes tested", culling.objectsTested,
                            culling.subtreesCulled, culling.meshesTested);

        ImGui::SeparatorText("Level of detail");
        ImGui::SliderFloat("Max error", &m_scene.lodErrorPixels, 0.25f, 32.0f, "%.2f px", ImGuiSliderFlags_Logarithmic);
        ImGui::SetItemTooltip("Each primitive is drawn at its coarsest level whose simplification error covers at "
                              "most this many pixels on screen.");
        const char* lodNames[] = {"Automatic", "0 (full detail)", "1", "2", "3"};
        int lodChoice = m_scene.forcedLod + 1;
        if (ImGui::Combo("Level", &lodChoice, lodNames, static_cast<int>(std::size(lodNames))))
            m_scene.forcedLod = lodChoice - 1;
        ImGui::TextDisabled("Draws per level: %u / %u / %u / %u", stats.drawsPerLod[0], stats.drawsPerLod[1],
                            stats.drawsPerLod[2], stats.drawsPerLod[3]);

        ImGui::SeparatorText("Sky");
        Skybox& skybox = m_scene.skybox();
        ImGui::Checkbox("Show sky", &skybox.visible);
        ImGui::SliderFloat("Intensity", &skybox.intensity, 0.0f, 2.0f, "%.2f");
        ImGui::TextDisabled("Showing: %s", skybox.source().c_str());
        ImGui::SetNextItemWidth(ImGui::GetFontSize() * 14.0f);
        ImGui::InputTextWithHint("##SkyFolder", "Folder with px, nx, py, ny, pz, nz", m_skyboxFolder.data(),
                                 m_skyboxFolder.size());
        ImGui::SameLine();
        if (ImGui::Button("Browse..."))
            browseForSkybox();
        ImGui::SameLine();
        if (ImGui::Button("Load"))
            m_pendingSkyboxFolder = m_skyboxFolder.data();
        ImGui::SameLine();
        if (ImGui::Button("Procedural"))
            m_pendingProceduralSky = true;

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

    void ScenePanel::drawLightingSettings()
    {
        if (!ImGui::BeginPopup("Lighting Settings"))
            return;

        LightingSettings& lighting = m_scene.lighting;
        ImGui::SeparatorText("Lights");
        const uint32_t lights = m_scene.sceneLightCount();
        if (m_scene.usedDefaultLight())
            ImGui::TextDisabled("No lights in the scene: lit by the default sun");
        else
            ImGui::TextDisabled("%u light%s in the scene", lights, lights == 1 ? "" : "s");
        if (lights > kMaxLights)
            ImGui::TextColored({1.0f, 0.6f, 0.2f, 1.0f}, "Only the first %u are shaded", kMaxLights);
        ImGui::Checkbox("Default sun when the scene has no lights", &lighting.defaultLight);

        ImGui::SeparatorText("Reflectance");
        int model = static_cast<int>(lighting.model);
        const char* models[] = {lightingModelName(LightingModel::Lambert), lightingModelName(LightingModel::Phong),
                                lightingModelName(LightingModel::BlinnPhong), lightingModelName(LightingModel::CookTorrance)};
        if (ImGui::Combo("Model", &model, models, static_cast<int>(std::size(models))))
            lighting.model = static_cast<LightingModel>(model);
        ImGui::SetItemTooltip("Lambert: diffuse only. Phong: a highlight around the mirror direction. Blinn-Phong: "
                              "around the normal, using the half vector, which stretches correctly at grazing angles. "
                              "Cook-Torrance: microfacets with GGX's long-tailed highlight and Smith shadowing, as "
                              "glTF materials are authored for.");
        ImGui::BeginDisabled(lighting.model == LightingModel::Lambert || lighting.model == LightingModel::CookTorrance);
        ImGui::Checkbox("Energy conserving", &lighting.energyConserving);
        ImGui::SetItemTooltip("Normalises the highlight so the light it reflects in total does not depend on how "
                              "sharp it is. Off, smoother surfaces only get a smaller highlight; on, it also gets "
                              "brighter.");
        ImGui::EndDisabled();
        ImGui::Checkbox("Fresnel (Schlick)", &lighting.fresnel);
        ImGui::SetItemTooltip("Reflectance rises from 4%% facing the surface to 100%% at grazing angles. What is "
                              "reflected is taken from the diffuse light.");
        ImGui::Checkbox("Normal maps", &lighting.normalMaps);
        ImGui::SetItemTooltip("Bends the shading normal by each material's normal map, in the frame of the surface's "
                              "tangent, bitangent and normal.");
        ImGui::SameLine();
        ImGui::Checkbox("Occlusion", &lighting.occlusion);
        ImGui::SetItemTooltip("Darkens the ambient light in creases by each material's occlusion texture. Lights "
                              "are not affected: blocking them takes shadows.");
        ImGui::SameLine();
        ImGui::Checkbox("Emissive", &lighting.emissive);
        ImGui::SetItemTooltip("Adds the light each material gives off itself.");
        ImGui::Checkbox("Override roughness", &lighting.overrideRoughness);
        ImGui::SameLine();
        ImGui::BeginDisabled(!lighting.overrideRoughness);
        ImGui::SetNextItemWidth(ImGui::GetFontSize() * 8.0f);
        ImGui::SliderFloat("##Roughness", &lighting.roughness, 0.0f, 1.0f, "%.2f");
        ImGui::EndDisabled();
        ImGui::SetItemTooltip("Every material gets this roughness instead of its own. GGX uses alpha = roughness^2; "
                              "Phong and Blinn-Phong the exponent 2 / roughness^4 - 2.");
        ImGui::Checkbox("Override metallic", &lighting.overrideMetallic);
        ImGui::SameLine();
        ImGui::BeginDisabled(!lighting.overrideMetallic);
        ImGui::SetNextItemWidth(ImGui::GetFontSize() * 8.0f);
        ImGui::SliderFloat("##Metallic", &lighting.metallic, 0.0f, 1.0f, "%.2f");
        ImGui::EndDisabled();
        ImGui::SetItemTooltip("Every material gets this metallic instead of its own: 0 is a dielectric (4%% specular, "
                              "coloured diffuse), 1 a metal (base-coloured specular, no diffuse).");

        ImGui::SeparatorText("Ambient");
        Vec3 sky = linearToSrgb(lighting.ambientSky);
        if (ImGui::ColorEdit3("Sky", &sky.x, ImGuiColorEditFlags_NoInputs))
            lighting.ambientSky = srgbToLinear(sky);
        ImGui::SameLine();
        Vec3 ground = linearToSrgb(lighting.ambientGround);
        if (ImGui::ColorEdit3("Ground", &ground.x, ImGuiColorEditFlags_NoInputs))
            lighting.ambientGround = srgbToLinear(ground);
        ImGui::SliderFloat("Intensity", &lighting.ambientIntensity, 0.0f, 4.0f, "%.2f");
        ImGui::SetItemTooltip("Light from every direction, blended from the ground colour below to the sky colour "
                              "above. A stand-in until image-based lighting.");

        ImGui::SeparatorText("Exposure");
        ToneMapper& toneMapper = m_app.renderer().toneMapper();
        ImGui::SliderFloat("Exposure", &toneMapper.exposure, -8.0f, 8.0f, "%+.1f EV");
        ImGui::SetItemTooltip("Each step doubles (or halves) the light before the tone curve.");
        int op = static_cast<int>(toneMapper.op);
        const char* operators[] = {toneMapOperatorName(ToneMapOperator::Clamp),
                                   toneMapOperatorName(ToneMapOperator::Reinhard),
                                   toneMapOperatorName(ToneMapOperator::Aces),
                                   toneMapOperatorName(ToneMapOperator::KhronosPbrNeutral)};
        if (ImGui::Combo("Tone curve", &op, operators, static_cast<int>(std::size(operators))))
            toneMapper.op = static_cast<ToneMapOperator>(op);
        ImGui::SetItemTooltip("How brightness above 1 is brought onto the screen. Khronos PBR Neutral keeps colours "
                              "below about 0.8 unchanged, so materials look as authored.");
        if (toneMapper.passThrough)
            ImGui::TextDisabled("Off while a data view is shown (View > Shading)");

        ImGui::EndPopup();
    }

    void ScenePanel::placeLightIcons(ImVec2 viewMin, ImVec2 viewSize)
    {
        m_lightIcons.clear();
        if (!m_showLightIcons)
            return;
        const ScreenProjection project(m_camera.camera(), viewMin, viewSize);
        m_scene.scene().forEachActive([&](GameObject& object) {
            const LightComponent* light = object.getComponent<LightComponent>();
            if (!light || !light->enabled)
                return;
            if (const std::optional<ImVec2> position = project(object.transform().worldPosition()))
                m_lightIcons.push_back({object.handle(), position->x, position->y});
        });
    }

    void ScenePanel::drawLightIcons(ImVec2 viewMin, ImVec2 viewSize)
    {
        if (m_lightIcons.empty())
            return;
        const ScreenProjection project(m_camera.camera(), viewMin, viewSize);
        const ImU32 highlight = ImGui::ColorConvertFloat4ToU32({m_outline.color.x, m_outline.color.y, m_outline.color.z, 1.0f});
        ImDrawList* drawList = ImGui::GetWindowDrawList();
        drawList->PushClipRect(viewMin, viewMin + viewSize, true);

        for (const LightIcon& icon : m_lightIcons)
        {
            GameObject* object = m_scene.scene().find(icon.object);
            const LightComponent* component = object ? object->getComponent<LightComponent>() : nullptr;
            if (!component)
                continue;
            const LightDesc& light = component->light;
            const bool selected = m_selection.isSelected(*object);
            const Mat4& world = object->transform().worldMatrix();
            const Vec3 position = world.translationPart();
            const Vec3 direction = normalize(world.transformVector(Vec3::forward()));
            const ImU32 lineColor = selected ? highlight : IM_COL32(230, 230, 230, 160);

            // The light's reach, for the selected light only: its direction, range or cone.
            if (selected)
            {
                // A directional light has no position that matters; its arrow is a fixed share of the
                // distance to the camera, so it stays the same size on screen.
                const float toCamera = length(m_camera.camera().position - position);
                Vec3 u;
                Vec3 v;
                perpendicularBasis(direction, u, v);
                if (light.type == LightType::Directional)
                {
                    const Vec3 end = position + direction * (toCamera * 0.2f);
                    project.line(drawList, position, end, lineColor);
                    project.circle(drawList, end - direction * (toCamera * 0.02f), u, v, toCamera * 0.01f, lineColor);
                }
                else if (light.type == LightType::Point && light.range > 0.0f)
                {
                    project.circle(drawList, position, Vec3::right(), Vec3::up(), light.range, lineColor);
                    project.circle(drawList, position, Vec3::right(), Vec3{0.0f, 0.0f, 1.0f}, light.range, lineColor);
                    project.circle(drawList, position, Vec3::up(), Vec3{0.0f, 0.0f, 1.0f}, light.range, lineColor);
                }
                else if (light.type == LightType::Spot)
                {
                    const float reach = light.range > 0.0f ? light.range : toCamera * 0.3f;
                    const float outer = std::min(light.outerConeAngle, toRadians(89.0f));
                    const float inner = std::min(light.innerConeAngle, outer);
                    const Vec3 end = position + direction * (reach * std::cos(outer));
                    const float radius = reach * std::sin(outer);
                    project.circle(drawList, end, u, v, radius, lineColor);
                    for (int i = 0; i < 4; ++i)
                    {
                        const float angle = kHalfPi * static_cast<float>(i);
                        project.line(drawList, position, end + (u * std::cos(angle) + v * std::sin(angle)) * radius,
                                     lineColor);
                    }
                    const Vec3 innerEnd = position + direction * (reach * std::cos(inner));
                    project.circle(drawList, innerEnd, u, v, reach * std::sin(inner), (lineColor & 0x00FFFFFFu) | 0x60000000u);
                }
            }

            // The icon: a disc in the light's colour; a sun has rays, a spot light a stroke toward
            // where it points.
            const ImVec2 center{icon.x, icon.y};
            const ImU32 border = selected ? highlight : IM_COL32(20, 22, 26, 230);
            if (light.type == LightType::Directional)
            {
                for (int i = 0; i < 8; ++i)
                {
                    const float angle = kTwoPi * static_cast<float>(i) / 8.0f;
                    const ImVec2 ray{std::cos(angle), std::sin(angle)};
                    drawList->AddLine(center + ImVec2{ray.x * (kLightIconRadius + 2.0f), ray.y * (kLightIconRadius + 2.0f)},
                                      center + ImVec2{ray.x * (kLightIconRadius + 6.0f), ray.y * (kLightIconRadius + 6.0f)},
                                      border, 2.0f);
                }
            }
            else if (light.type == LightType::Spot)
            {
                if (const std::optional<ImVec2> ahead = project(position + direction * 0.01f))
                {
                    ImVec2 delta = *ahead - center;
                    const float pixels = std::sqrt(delta.x * delta.x + delta.y * delta.y);
                    if (pixels > 1e-4f)
                    {
                        delta = {delta.x / pixels, delta.y / pixels};
                        drawList->AddLine(center, center + ImVec2{delta.x * (kLightIconRadius + 8.0f), delta.y * (kLightIconRadius + 8.0f)},
                                          border, 2.0f);
                    }
                }
            }
            drawList->AddCircleFilled(center, kLightIconRadius, iconColor(light.color));
            drawList->AddCircle(center, kLightIconRadius, border, 0, selected ? 2.5f : 1.5f);
        }
        drawList->PopClipRect();
    }

    GameObject* ScenePanel::lightIconAt(ImVec2 mouse) const
    {
        const float reach = kLightIconRadius + 3.0f;
        GameObject* nearest = nullptr;
        float nearestDistance = reach * reach;
        for (const LightIcon& icon : m_lightIcons)
        {
            const float dx = mouse.x - icon.x;
            const float dy = mouse.y - icon.y;
            const float distance = dx * dx + dy * dy;
            if (distance <= nearestDistance)
            {
                if (GameObject* object = m_scene.scene().find(icon.object))
                {
                    nearest = object;
                    nearestDistance = distance;
                }
            }
        }
        return nearest;
    }
}

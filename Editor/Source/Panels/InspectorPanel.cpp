#include "Panels/InspectorPanel.h"

#include "EditorSelection.h"
#include "ShadingMenu.h"
#include "Core/Log.h"
#include "Math/Color.h"
#include "RHI/Texture.h"
#include "Renderer/Model.h"
#include "Scene/LightComponent.h"
#include "Scene/MeshComponent.h"
#include "Scene/SceneModule.h"

#include <imgui.h>

#include <algorithm>
#include <array>
#include <cstring>
#include <string>

namespace arc
{
    namespace
    {
        const char* alphaModeName(AlphaMode mode)
        {
            switch (mode)
            {
                case AlphaMode::Opaque: return "opaque";
                case AlphaMode::Mask:   return "mask";
                case AlphaMode::Blend:  return "blend";
            }
            return "";
        }

        void copyToBuffer(std::array<char, 256>& buffer, const std::string& text)
        {
            const size_t length = std::min(text.size(), buffer.size() - 1);
            std::memcpy(buffer.data(), text.data(), length);
            buffer[length] = '\0';
        }

        // A labelled row: the label in a fixed-width column on the left, the widget filling the rest.
        void beginRow(const char* label)
        {
            ImGui::AlignTextToFramePadding();
            ImGui::TextUnformatted(label);
            ImGui::SameLine(ImGui::GetFontSize() * 5.5f);
            ImGui::SetNextItemWidth(-1.0f);
        }
    }

    InspectorPanel::InspectorPanel(SceneModule& scene, EditorSelection& selection)
        : m_scene(scene)
        , m_selection(selection)
    {
        m_editors.push_back({
            .type = &MeshComponent::staticType(),
            .create = [] { return std::make_unique<MeshComponent>(); },
            .draw = [this](Component& component) { drawMeshComponent(component); },
        });
        m_editors.push_back({
            .type = &LightComponent::staticType(),
            .create = [] { return std::make_unique<LightComponent>(defaultLightDesc(LightType::Point)); },
            .draw = [this](Component& component) { drawLightComponent(component); },
        });
    }

    const InspectorPanel::ComponentEditor* InspectorPanel::editorFor(const ComponentType& type) const
    {
        const auto it = std::find_if(m_editors.begin(), m_editors.end(), [&type](const ComponentEditor& e) { return e.type == &type; });
        return it != m_editors.end() ? &*it : nullptr;
    }

    void InspectorPanel::draw(bool* open)
    {
        ImGui::SetNextWindowSize({340.0f, 520.0f}, ImGuiCond_FirstUseEver);
        if (!ImGui::Begin(title(), open))
        {
            ImGui::End();
            return;
        }

        GameObject* object = m_selection.object();
        if (!object)
        {
            ImGui::TextDisabled("Select an object in the Hierarchy.");
            ImGui::End();
            return;
        }

        ImGui::PushID(object->uuid().toString().c_str());
        drawHeader(*object);
        drawTransform(*object);
        drawComponents(*object);
        drawAddComponent(*object);
        drawHierarchyShading();
        ImGui::PopID();
        ImGui::End();
    }

    void InspectorPanel::drawHeader(GameObject& object)
    {
        bool active = object.isActiveSelf();
        if (ImGui::Checkbox("##Active", &active))
            object.setActive(active);
        ImGui::SetItemTooltip("Active. Inactive objects, and everything below them, are not updated or drawn.");
        ImGui::SameLine();

        if (m_nameFor != object.handle())
        {
            copyToBuffer(m_nameBuffer, object.name());
            m_nameFor = object.handle();
        }
        ImGui::SetNextItemWidth(-1.0f);
        if (ImGui::InputText("##Name", m_nameBuffer.data(), m_nameBuffer.size()))
            object.setName(m_nameBuffer.data());

        ImGui::TextDisabled("UUID %s", object.uuid().toString().c_str());
        if (!active || !object.isActiveInHierarchy())
        {
            ImGui::SameLine();
            ImGui::TextDisabled(active ? "(inactive parent)" : "(inactive)");
        }
    }

    void InspectorPanel::drawTransform(GameObject& object)
    {
        if (!ImGui::CollapsingHeader("Transform", ImGuiTreeNodeFlags_DefaultOpen))
            return;
        if (ImGui::BeginPopupContextItem("##TransformMenu"))
        {
            if (ImGui::MenuItem("Reset"))
                object.transform().setLocal(Vec3::zero(), Quat::identity(), Vec3::one());
            ImGui::EndPopup();
        }

        Transform& transform = object.transform();

        Vec3 position = transform.localPosition();
        beginRow("Position");
        if (ImGui::DragFloat3("##Position", &position.x, 0.05f, 0.0f, 0.0f, "%.3f"))
            transform.setLocalPosition(position);

        // Recomputed only when the quaternion was changed by something other than this field.
        if (m_eulerFor != object.handle() || !(transform.localRotation() == m_eulerSource))
        {
            const Vec3 radians = transform.localRotation().toEuler();
            m_eulerDegrees = {toDegrees(radians.x), toDegrees(radians.y), toDegrees(radians.z)};
            m_eulerFor = object.handle();
        }
        beginRow("Rotation");
        if (ImGui::DragFloat3("##Rotation", &m_eulerDegrees.x, 0.5f, 0.0f, 0.0f, "%.1f deg"))
            transform.setLocalRotation(Quat::fromEuler(
                {toRadians(m_eulerDegrees.x), toRadians(m_eulerDegrees.y), toRadians(m_eulerDegrees.z)}));
        ImGui::SetItemTooltip("Pitch (X), yaw (Y) and roll (Z), applied as yaw, then pitch, then roll.");
        m_eulerSource = transform.localRotation();

        Vec3 scale = transform.localScale();
        beginRow("Scale");
        if (ImGui::DragFloat3("##Scale", &scale.x, 0.01f, 0.0f, 0.0f, "%.3f"))
            transform.setLocalScale(scale);

        const Vec3 world = transform.worldPosition();
        ImGui::TextDisabled("World position (%.3f, %.3f, %.3f)", world.x, world.y, world.z);
    }

    void InspectorPanel::drawComponents(GameObject& object)
    {
        const Component* toRemove = nullptr;
        for (const std::unique_ptr<Component>& component : object.components())
        {
            ImGui::PushID(component.get());
            bool keep = true;
            const std::string label(component->type().name);
            if (ImGui::CollapsingHeader(label.c_str(), &keep, ImGuiTreeNodeFlags_DefaultOpen))
            {
                ImGui::Checkbox("Enabled", &component->enabled);
                if (const ComponentEditor* editor = editorFor(component->type()))
                    editor->draw(*component);
                else
                    ImGui::TextDisabled("No editor for this component.");
            }
            if (!keep)
                toRemove = component.get();
            ImGui::PopID();
        }
        // Removed after the loop: removing erases from the list being iterated.
        if (toRemove)
            object.removeComponent(*toRemove);
    }

    void InspectorPanel::drawAddComponent(GameObject& object)
    {
        ImGui::Spacing();
        ImGui::Separator();
        const float width = ImGui::GetFontSize() * 10.0f;
        ImGui::SetCursorPosX(std::max(0.0f, (ImGui::GetContentRegionAvail().x - width) * 0.5f));
        if (ImGui::Button("Add Component", {width, 0.0f}))
            ImGui::OpenPopup("AddComponent");
        if (ImGui::BeginPopup("AddComponent"))
        {
            for (const ComponentEditor& editor : m_editors)
            {
                const std::string name(editor.type->name);
                if (ImGui::MenuItem(name.c_str()))
                    object.addComponent(editor.create());
            }
            ImGui::EndPopup();
        }
    }

    void InspectorPanel::drawMeshComponent(Component& component)
    {
        auto& mesh = static_cast<MeshComponent&>(component);
        const std::shared_ptr<const Model>& model = mesh.model();

        beginRow("Model");
        if (ImGui::BeginCombo("##Model", model ? model->name().c_str() : "(none)"))
        {
            for (const std::shared_ptr<const Model>& loaded : m_scene.loadedModels())
                if (ImGui::Selectable(loaded->name().c_str(), loaded == model))
                    mesh.setMesh(loaded, 0);
            ImGui::EndCombo();
        }
        ImGui::SetItemTooltip("Models that are loaded. Import more with File > Import Model, or by dropping a "
                              ".gltf or .glb file on the window.");
        if (!model)
            return;

        const auto& meshes = model->meshes();
        const char* meshName = mesh.hasMesh() ? meshes[mesh.meshIndex()].name.c_str() : "(none)";
        beginRow("Mesh");
        if (ImGui::BeginCombo("##Mesh", meshName[0] ? meshName : "(unnamed)"))
        {
            for (uint32_t i = 0; i < meshes.size(); ++i)
            {
                ImGui::PushID(static_cast<int>(i));
                const std::string label = meshes[i].name.empty() ? "Mesh " + std::to_string(i) : meshes[i].name;
                if (ImGui::Selectable(label.c_str(), i == mesh.meshIndex()))
                    mesh.setMesh(model, i);
                ImGui::PopID();
            }
            ImGui::EndCombo();
        }
        if (!mesh.hasMesh())
            return;

        const MeshData& data = meshes[mesh.meshIndex()];
        const Vec3 size = mesh.localBounds().size();
        ImGui::TextDisabled("%u primitives, %u triangles", data.primitiveCount, model->meshTriangleCount(mesh.meshIndex()));
        ImGui::TextDisabled("Size %.2f x %.2f x %.2f (object space)", size.x, size.y, size.z);
        ImGui::TextDisabled("Model textures: %u, %.1f MB on the GPU", model->textureCount(),
                            static_cast<double>(model->textureBytes()) / (1024.0 * 1024.0));

        if (ImGui::TreeNode("Materials"))
        {
            for (uint32_t i = 0; i < data.primitiveCount; ++i)
            {
                const Primitive& primitive = model->primitives()[data.firstPrimitive + i];
                ImGui::PushID(static_cast<int>(i));
                if (primitive.material < 0)
                {
                    ImGui::BulletText("Primitive %u: default material", i);
                    ImGui::PopID();
                    continue;
                }
                const MaterialData& material = model->materials()[static_cast<size_t>(primitive.material)];
                const Vec4& c = material.baseColorFactor;
                ImGui::ColorButton("##Color", {c.x, c.y, c.z, c.w},
                                   ImGuiColorEditFlags_NoTooltip | ImGuiColorEditFlags_AlphaPreviewHalf, {14.0f, 14.0f});
                ImGui::SameLine();
                // A texture slot as its format, so what the importer chose shows.
                const auto has = [&](int32_t index) -> const char* {
                    if (index < 0)
                        return "none";
                    const rhi::Texture* slot = model->texture(index);
                    return slot ? rhi::formatName(slot->desc().format) : "missing";
                };
                ImGui::Text("%s: base colour %s, %s%s", material.name.empty() ? "(unnamed)" : material.name.c_str(),
                            has(material.baseColorTexture), alphaModeName(material.alphaMode),
                            material.doubleSided ? ", double-sided" : "");
                ImGui::TextDisabled("    metallic %.2f, roughness %.2f, texture %s", material.metallicFactor,
                                    material.roughnessFactor, has(material.metallicRoughnessTexture));
                ImGui::TextDisabled("    normal map %s, occlusion %s, emissive %s", has(material.normalTexture),
                                    has(material.occlusionTexture), has(material.emissiveTexture));
                ImGui::PopID();
            }
            ImGui::TreePop();
        }

        drawMaterialStyles(mesh);
    }

    void InspectorPanel::drawHierarchyShading()
    {
        ImGui::Spacing();
        if (!ImGui::CollapsingHeader("Shading (with children)"))
            return;
        drawShadingMenuItems(m_selection, m_scene.scene());
    }

    void InspectorPanel::drawMaterialStyles(MeshComponent& mesh)
    {
        const Model& model = *mesh.model();
        const MeshData& data = model.meshes()[mesh.meshIndex()];
        // Each material once, in the order the mesh's primitives first use it.
        std::vector<uint32_t> materials;
        for (uint32_t i = 0; i < data.primitiveCount; ++i)
        {
            const int32_t material = model.primitives()[data.firstPrimitive + i].material;
            if (material >= 0 && std::find(materials.begin(), materials.end(), static_cast<uint32_t>(material)) == materials.end())
                materials.push_back(static_cast<uint32_t>(material));
        }
        if (materials.empty() || !ImGui::TreeNode("Style"))
            return;
        ImGui::TextDisabled("For this object only; the model is shared.");

        for (const ShadingModel shading : {ShadingModel::Physical, ShadingModel::Toon})
        {
            const std::string label = std::string("All ") + shadingModelName(shading);
            if (ImGui::SmallButton(label.c_str()))
                for (const uint32_t material : materials)
                {
                    MaterialStyle style = mesh.materialStyle(material);
                    style.shading = shading;
                    style.outline.enabled = shading == ShadingModel::Toon;
                    mesh.setMaterialStyle(material, style);
                }
            ImGui::SameLine();
        }
        ImGui::NewLine();

        for (const uint32_t material : materials)
        {
            ImGui::PushID(static_cast<int>(material));
            const std::string& name = model.materials()[material].name;
            ImGui::SeparatorText(name.empty() ? "(unnamed material)" : name.c_str());
            MaterialStyle style = mesh.materialStyle(material);
            bool changed = false;

            int shading = static_cast<int>(style.shading);
            const char* shadingNames[] = {shadingModelName(ShadingModel::Physical), shadingModelName(ShadingModel::Toon)};
            beginRow("Shading");
            changed |= ImGui::Combo("##Shading", &shading, shadingNames, static_cast<int>(std::size(shadingNames)));
            style.shading = static_cast<ShadingModel>(shading);

            if (style.shading == ShadingModel::Toon)
            {
                ToonStyle& toon = style.toon;
                const bool hasRamp = !mesh.toonRampPath(material).empty();
                ImGui::BeginDisabled(hasRamp);
                int bands = static_cast<int>(toon.bands);
                beginRow("Bands");
                if (ImGui::SliderInt("##Bands", &bands, 1, 8))
                {
                    toon.bands = static_cast<uint32_t>(bands);
                    changed = true;
                }
                ImGui::SetItemTooltip("Steps of light above the shadow: 1 is two-tone.");
                ImGui::EndDisabled();
                beginRow("Softness");
                changed |= ImGui::SliderFloat("##Softness", &toon.softness, 0.0f, 0.25f, "%.3f");
                ImGui::SetItemTooltip("Width of each band's edge, and of the highlight's and rim's.");
                beginRow("Highlight");
                changed |= ImGui::SliderFloat("##Highlight", &toon.specular, 0.0f, 4.0f, "%.2f");
                ImGui::SetItemTooltip("A hard-edged highlight sized by the material's roughness.");
                beginRow("Rim size");
                changed |= ImGui::SliderFloat("##RimSize", &toon.rimSize, 0.0f, 1.0f, "%.2f");
                beginRow("Rim");
                changed |= ImGui::SliderFloat("##Rim", &toon.rim, 0.0f, 4.0f, "%.2f");
                ImGui::SetItemTooltip("Light along the silhouette on the side facing each light.");

                // ImGui keeps its own copy of the text while the field is being edited, so a buffer
                // filled from the current path each frame is enough; Enter applies it.
                std::array<char, 512> ramp{};
                const std::string& rampPath = mesh.toonRampPath(material);
                std::memcpy(ramp.data(), rampPath.data(), std::min(rampPath.size(), ramp.size() - 1));
                beginRow("Ramp");
                if (ImGui::InputTextWithHint("##Ramp", "texture path, Enter to load", ramp.data(), ramp.size(),
                                             ImGuiInputTextFlags_EnterReturnsTrue))
                {
                    const std::string path = ramp.data();
                    mesh.setToonRamp(material, path, path.empty() ? nullptr : m_scene.loadTexture(path));
                }
                ImGui::SetItemTooltip("A texture read across its width by how directly light falls on the surface "
                                      "(left: facing away, right: facing it), in place of bands.");
                if (hasRamp)
                {
                    ImGui::SameLine();
                    if (ImGui::SmallButton("Clear"))
                        mesh.setToonRamp(material, {}, nullptr);
                }
            }

            OutlineStyle& outline = style.outline;
            beginRow("Outline");
            changed |= ImGui::Checkbox("##Outline", &outline.enabled);
            ImGui::BeginDisabled(!outline.enabled);
            ImGui::SameLine();
            ImGui::SetNextItemWidth(ImGui::GetFontSize() * 6.0f);
            changed |= ImGui::SliderFloat("##OutlineWidth", &outline.width, 0.5f, 8.0f, "%.1f px");
            ImGui::SameLine();
            Vec3 color = linearToSrgb(outline.color);
            if (ImGui::ColorEdit3("##OutlineColor", &color.x, ImGuiColorEditFlags_NoInputs))
            {
                outline.color = srgbToLinear(color);
                changed = true;
            }
            ImGui::EndDisabled();
            ImGui::SetItemTooltip("Inverted hull: the mesh drawn again behind itself, grown along its normals. "
                                  "Hard edges, with a normal per face, open gaps in it.");

            if (changed)
                mesh.setMaterialStyle(material, style);

            if (GameObject* object = m_selection.object(); object && ImGui::SmallButton("Use for Everything Below"))
            {
                const uint32_t count = copyStyleToHierarchy(*object, mesh.materialStyle(material));
                ARC_LOG_INFO("Style of '{}' copied to {} material{} in '{}'", name, count, count == 1 ? "" : "s",
                             object->name());
            }
            ImGui::SetItemTooltip("Gives every material of this object and its children this style, ramp texture "
                                  "aside.");
            ImGui::PopID();
        }
        ImGui::TreePop();
    }

    void InspectorPanel::drawLightComponent(Component& component)
    {
        LightDesc& light = static_cast<LightComponent&>(component).light;

        beginRow("Type");
        if (ImGui::BeginCombo("##Type", lightTypeName(light.type)))
        {
            for (const LightType type : {LightType::Directional, LightType::Point, LightType::Spot})
            {
                if (ImGui::Selectable(lightTypeName(type), type == light.type) && type != light.type)
                {
                    // Directional intensities are in lux and the others in candela, so a value
                    // carried over would mean something else entirely.
                    const LightDesc defaults = defaultLightDesc(type);
                    light.type = type;
                    light.intensity = defaults.intensity;
                }
            }
            ImGui::EndCombo();
        }

        // The picker shows sRGB-encoded values, as image editors do; the light stores linear colour.
        Vec3 color = linearToSrgb(light.color);
        beginRow("Colour");
        if (ImGui::ColorEdit3("##Colour", &color.x, ImGuiColorEditFlags_NoInputs))
            light.color = srgbToLinear(color);

        const bool directional = light.type == LightType::Directional;
        beginRow("Intensity");
        ImGui::DragFloat("##Intensity", &light.intensity, 0.05f, 0.0f, 1.0e6f, directional ? "%.2f lux" : "%.2f cd",
                         ImGuiSliderFlags_AlwaysClamp | ImGuiSliderFlags_Logarithmic);
        ImGui::SetItemTooltip(directional ? "Illuminance on a surface facing the light, in lux (lm/m^2)."
                                          : "Luminous intensity in candela (lm/sr): the light reaching a surface "
                                            "1 m away, facing it, is this many lux, and falls off with the square "
                                            "of the distance.");

        if (!directional)
        {
            beginRow("Range");
            ImGui::DragFloat("##Range", &light.range, 0.05f, 0.0f, 1.0e5f, light.range > 0.0f ? "%.2f m" : "No limit",
                             ImGuiSliderFlags_AlwaysClamp);
            ImGui::SetItemTooltip("Distance at which the light has faded to zero. 0 for no limit: the light then "
                                  "reaches every surface, only ever dimmer.");
        }

        if (light.type == LightType::Spot)
        {
            float outer = toDegrees(light.outerConeAngle);
            float inner = toDegrees(light.innerConeAngle);
            beginRow("Outer cone");
            if (ImGui::DragFloat("##Outer", &outer, 0.25f, 0.1f, 90.0f, "%.1f deg", ImGuiSliderFlags_AlwaysClamp))
            {
                light.outerConeAngle = toRadians(outer);
                light.innerConeAngle = std::min(light.innerConeAngle, light.outerConeAngle);
            }
            ImGui::SetItemTooltip("Angle from the axis at which the light reaches zero.");
            beginRow("Inner cone");
            if (ImGui::DragFloat("##Inner", &inner, 0.25f, 0.0f, outer, "%.1f deg", ImGuiSliderFlags_AlwaysClamp))
                light.innerConeAngle = std::min(toRadians(inner), light.outerConeAngle);
            ImGui::SetItemTooltip("Angle from the axis at which the light starts to fade.");
        }

        if (light.type != LightType::Point)
            ImGui::TextDisabled("Shines along the object's -Z axis.");
    }
}

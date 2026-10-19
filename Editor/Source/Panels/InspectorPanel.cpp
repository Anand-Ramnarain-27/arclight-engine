#include "Panels/InspectorPanel.h"

#include "EditorSelection.h"
#include "Renderer/Model.h"
#include "Scene/MeshComponent.h"
#include "Scene/SceneModule.h"

#include <imgui.h>

#include <algorithm>
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
                const char* texture = material.baseColorTexture < 0                       ? "no texture"
                                      : model->texture(material.baseColorTexture) != nullptr ? "texture"
                                                                                             : "texture missing";
                ImGui::Text("%s: %s, %s%s", material.name.empty() ? "(unnamed)" : material.name.c_str(), texture,
                            alphaModeName(material.alphaMode), material.doubleSided ? ", double-sided" : "");
                ImGui::PopID();
            }
            ImGui::TreePop();
        }
    }
}

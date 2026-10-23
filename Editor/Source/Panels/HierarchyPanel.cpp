#include "Panels/HierarchyPanel.h"

#include "EditorSelection.h"
#include "Scene/EditorCamera.h"
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
        constexpr const char* kPayloadType = "ARC_GAMEOBJECT";

        // Dropping in the top or bottom quarter of a row inserts next to it; the middle half
        // makes the dragged object a child.
        constexpr float kEdgeFraction = 0.25f;

        GameObject* payloadObject(const ImGuiPayload& payload, const Scene& scene)
        {
            GameObjectHandle handle;
            if (payload.DataSize != sizeof(handle))
                return nullptr;
            std::memcpy(&handle, payload.Data, sizeof(handle));
            return scene.find(handle);
        }

        GameObjectHandle handleOf(const GameObject* object)
        {
            return object ? object->handle() : GameObjectHandle{};
        }
    }

    void HierarchyPanel::draw(bool* open)
    {
        ImGui::SetNextWindowSize({280.0f, 480.0f}, ImGuiCond_FirstUseEver);
        if (!ImGui::Begin(title(), open))
        {
            ImGui::End();
            return;
        }

        Scene& scene = m_scene.scene();
        if (ImGui::SmallButton("+ Create"))
            ImGui::OpenPopup("Create");
        if (ImGui::BeginPopup("Create"))
        {
            if (ImGui::MenuItem("Empty"))
                m_selection.createEmpty(nullptr);
            if (ImGui::MenuItem("Empty Child", nullptr, false, m_selection.object() != nullptr))
                m_selection.createEmpty(m_selection.object());
            ImGui::EndPopup();
        }
        ImGui::SameLine();
        ImGui::TextDisabled("%u objects", scene.objectCount());
        ImGui::Separator();

        ImGui::BeginChild("##Tree");
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, {ImGui::GetStyle().ItemSpacing.x, 2.0f});
        for (GameObject* root : scene.roots())
            drawNode(*root);
        ImGui::PopStyleVar();
        m_selection.revealRequested = false;

        // The rest of the panel: clicking clears the selection, and dropping moves the object to
        // the end of the top level.
        const ImVec2 rest = ImGui::GetContentRegionAvail();
        ImGui::InvisibleButton("##Background", {std::max(rest.x, 1.0f), std::max(rest.y, ImGui::GetFrameHeight())});
        if (ImGui::IsItemClicked(ImGuiMouseButton_Left))
            m_selection.clear();
        if (ImGui::BeginDragDropTarget())
        {
            if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(kPayloadType))
                if (const GameObject* dragged = payloadObject(*payload, scene))
                    m_pendingMove = Move{.object = dragged->handle(), .newParent = {}, .index = GameObject::kAppend};
            ImGui::EndDragDropTarget();
        }
        if (ImGui::BeginPopupContextItem("##BackgroundMenu"))
        {
            if (ImGui::MenuItem("Create Empty"))
                m_selection.createEmpty(nullptr);
            ImGui::EndPopup();
        }
        ImGui::EndChild();

        applyPending();
        ImGui::End();
    }

    void HierarchyPanel::drawNode(GameObject& object)
    {
        const bool leaf = object.children().empty();
        ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_SpanAvailWidth;
        if (leaf)
            flags |= ImGuiTreeNodeFlags_Leaf;
        if (m_selection.isSelected(object))
            flags |= ImGuiTreeNodeFlags_Selected;

        // Expands every ancestor of an object selected elsewhere (a new child, a duplicate).
        const GameObject* selected = m_selection.object();
        if (m_selection.revealRequested && selected && selected->isDescendantOf(object))
            ImGui::SetNextItemOpen(true);

        // Keyed by UUID, so expanded nodes stay expanded across sessions once scenes are saved.
        const std::string id = object.uuid().toString();
        const bool inactive = !object.isActiveInHierarchy();
        if (inactive)
            ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
        const bool open = ImGui::TreeNodeEx(id.c_str(), flags, "%s", object.name().c_str());
        if (inactive)
            ImGui::PopStyleColor();

        if (m_selection.revealRequested && selected == &object)
            ImGui::SetScrollHereY();

        if (ImGui::IsItemClicked(ImGuiMouseButton_Left) && !ImGui::IsItemToggledOpen())
            m_selection.select(&object);
        if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
        {
            m_pendingAction = Action::Frame;
            m_actionTarget = object.handle();
        }
        if (object.getComponent<MeshComponent>() && ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal))
            ImGui::SetTooltip("Has a mesh");

        if (ImGui::BeginDragDropSource())
        {
            const GameObjectHandle handle = object.handle();
            ImGui::SetDragDropPayload(kPayloadType, &handle, sizeof(handle));
            ImGui::TextUnformatted(object.name().c_str());
            ImGui::EndDragDropSource();
        }
        dropOnto(object, open && !leaf);

        if (ImGui::BeginPopupContextItem())
        {
            if (ImGui::IsWindowAppearing())
                m_selection.select(&object);
            const auto request = [this, &object](Action action) {
                m_pendingAction = action;
                m_actionTarget = object.handle();
            };
            if (ImGui::MenuItem("Create Empty Child"))
                request(Action::CreateChild);
            if (ImGui::MenuItem("Duplicate", "Ctrl+D"))
                request(Action::Duplicate);
            if (ImGui::MenuItem("Delete", "Del"))
                request(Action::Destroy);
            ImGui::Separator();
            if (ImGui::MenuItem("Frame in Scene View"))
                request(Action::Frame);
            if (ImGui::MenuItem("Save as Prefab...") && onSavePrefab)
                onSavePrefab(object);
            ImGui::EndPopup();
        }

        if (open)
        {
            for (GameObject* child : object.children())
                drawNode(*child);
            ImGui::TreePop();
        }
    }

    void HierarchyPanel::dropOnto(GameObject& target, bool targetOpen)
    {
        if (!ImGui::BeginDragDropTarget())
            return;

        // Accepted before delivery so the drop position can be shown while dragging; the default
        // highlight is replaced by a line or a box that says where the object will go.
        const ImGuiDragDropFlags flags = ImGuiDragDropFlags_AcceptBeforeDelivery | ImGuiDragDropFlags_AcceptNoDrawDefaultRect;
        const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(kPayloadType, flags);
        const GameObject* dragged = payload ? payloadObject(*payload, m_scene.scene()) : nullptr;
        if (dragged && dragged != &target && !target.isDescendantOf(*dragged))
        {
            const ImVec2 min = ImGui::GetItemRectMin();
            const ImVec2 max = ImGui::GetItemRectMax();
            const float t = (ImGui::GetMousePos().y - min.y) / std::max(max.y - min.y, 1.0f);

            // Below an expanded node, its first child is what follows on screen, so "after" there
            // would put the object somewhere other than where the line is drawn.
            Move move{.object = dragged->handle(), .newParent = target.handle(), .index = GameObject::kAppend};
            ImDrawList* drawList = ImGui::GetWindowDrawList();
            const ImU32 color = ImGui::GetColorU32(ImGuiCol_DragDropTarget);
            if (t < kEdgeFraction || (t > 1.0f - kEdgeFraction && !targetOpen))
            {
                const bool before = t < kEdgeFraction;
                move.newParent = handleOf(target.parent());
                move.index = target.siblingIndex() + (before ? 0 : 1);
                const float y = before ? min.y : max.y;
                drawList->AddLine({min.x, y}, {max.x, y}, color, 2.0f);
            }
            else
            {
                drawList->AddRect(min, max, color, 2.0f, 2.0f);
            }

            if (payload->IsDelivery())
                m_pendingMove = move;
        }
        ImGui::EndDragDropTarget();
    }

    void HierarchyPanel::applyPending()
    {
        Scene& scene = m_scene.scene();
        if (m_pendingMove)
        {
            GameObject* object = scene.find(m_pendingMove->object);
            GameObject* parent = scene.find(m_pendingMove->newParent);
            const bool parentValid = m_pendingMove->newParent.isNull() || parent != nullptr;
            if (object && parentValid)
                object->setParent(parent, true, m_pendingMove->index);
            m_pendingMove.reset();
        }

        GameObject* target = scene.find(m_actionTarget);
        if (target)
        {
            switch (m_pendingAction)
            {
                case Action::CreateChild:
                    m_selection.createEmpty(target);
                    break;
                case Action::Duplicate:
                    m_selection.select(target);
                    m_selection.duplicateSelected();
                    break;
                case Action::Destroy:
                    m_selection.select(target);
                    m_selection.destroySelected();
                    break;
                case Action::Frame:
                    m_selection.select(target);
                    m_camera.frame(hierarchyBounds(*target), target->transform().worldPosition());
                    break;
                case Action::None:
                    break;
            }
        }
        m_pendingAction = Action::None;
        m_actionTarget = {};
    }
}

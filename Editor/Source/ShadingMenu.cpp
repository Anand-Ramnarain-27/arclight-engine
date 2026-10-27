#include "ShadingMenu.h"

#include "Core/Log.h"
#include "EditorSelection.h"
#include "Scene/MeshComponent.h"
#include "Scene/Scene.h"

#include <imgui.h>

#include <cstddef>
#include <cstdint>
#include <iterator>
#include <optional>
#include <span>

namespace arc
{
    namespace
    {
        struct Choice
        {
            const char* label;
            StyleChange change;
        };

        // Toon alone keeps each material's outline as it is; the others say what they set.
        const Choice kChoices[] = {
            {"Toon", {.shading = ShadingModel::Toon, .outline = std::nullopt}},
            {"Toon with Outlines", {.shading = ShadingModel::Toon, .outline = true}},
            {"Physical", {.shading = ShadingModel::Physical, .outline = false}},
            {"Outlines On", {.shading = std::nullopt, .outline = true}},
            {"Outlines Off", {.shading = std::nullopt, .outline = false}},
        };

        // The menu item that was clicked, or null.
        const Choice* drawChoices(bool enabled)
        {
            const Choice* clicked = nullptr;
            for (size_t i = 0; i < std::size(kChoices); ++i)
            {
                // The outline-only choices after the shading ones.
                if (i == 3)
                    ImGui::Separator();
                if (ImGui::MenuItem(kChoices[i].label, nullptr, false, enabled))
                    clicked = &kChoices[i];
            }
            return clicked;
        }

        void report(const Choice& choice, uint32_t materials, const char* where)
        {
            ARC_LOG_INFO("{}: {} material{} changed in {}", choice.label, materials, materials == 1 ? "" : "s", where);
        }
    }

    void drawShadingMenuItems(EditorSelection& selection, Scene& scene)
    {
        GameObject* selected = selection.object();
        ImGui::TextDisabled(selected ? "Selected object and its children" : "Select an object, or:");
        if (const Choice* choice = drawChoices(selected != nullptr); choice && selected)
            report(*choice, changeStyles(*selected, choice->change), selected->name().c_str());

        ImGui::Separator();
        if (ImGui::BeginMenu("Whole Scene"))
        {
            if (const Choice* choice = drawChoices(true))
            {
                uint32_t materials = 0;
                for (GameObject* root : scene.roots())
                    materials += changeStyles(*root, choice->change);
                report(*choice, materials, "the scene");
            }
            ImGui::EndMenu();
        }
    }
}

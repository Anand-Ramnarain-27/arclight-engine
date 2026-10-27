#pragma once

namespace arc
{
    class EditorSelection;
    class Scene;

    /// Menu items that switch the shading and outlines of every material below the selected object,
    /// or in the whole scene, in one go: an imported room is a hierarchy of tens of meshes, too many
    /// to style one by one in the Inspector. Call between ImGui::BeginMenu() (or BeginPopup()) and
    /// the matching End.
    void drawShadingMenuItems(EditorSelection& selection, Scene& scene);
}

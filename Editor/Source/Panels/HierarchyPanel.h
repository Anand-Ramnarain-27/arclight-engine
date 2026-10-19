#pragma once

#include "Panels/Panel.h"
#include "Scene/GameObject.h"

#include <cstddef>
#include <optional>

namespace arc
{
    class EditorCamera;
    class EditorSelection;
    class SceneModule;

    /// The scene's objects as a tree.
    ///
    ///   Click              Select
    ///   Double-click       Select and frame in the Scene view
    ///   Drag onto a row    Make the dragged object its child (middle of the row), or move it
    ///                      before or after it (top or bottom edge)
    ///   Drag onto the      Move to the end of the top level
    ///   empty space
    ///   Right-click        Create, duplicate, delete
    ///
    /// Reparenting keeps world transforms. Inactive objects, and everything below them, are greyed.
    /// Every change is applied after the tree has been drawn, so the hierarchy never changes while
    /// it is being walked.
    class HierarchyPanel final : public Panel
    {
    public:
        HierarchyPanel(SceneModule& scene, EditorSelection& selection, EditorCamera& camera)
            : m_scene(scene)
            , m_selection(selection)
            , m_camera(camera)
        {
        }

        [[nodiscard]] const char* title() const override { return "Hierarchy"; }
        void draw(bool* open) override;

    private:
        enum class Action
        {
            None,
            CreateChild,
            Duplicate,
            Destroy,
            Frame,
        };

        /// A reparenting requested by a drop, applied after the tree is drawn.
        struct Move
        {
            GameObjectHandle object;
            GameObjectHandle newParent;   // Null for the top level.
            size_t index = GameObject::kAppend;
        };

        void drawNode(GameObject& object);
        void dropOnto(GameObject& target, bool targetOpen);
        void applyPending();

        SceneModule& m_scene;
        EditorSelection& m_selection;
        EditorCamera& m_camera;

        std::optional<Move> m_pendingMove;
        Action m_pendingAction = Action::None;
        GameObjectHandle m_actionTarget;
    };
}

#pragma once

#include "Math/Math.h"

struct ImVec2;

namespace arc
{
    class GameObject;
    struct Camera;

    /// Move, rotate and scale handles on the selected object, drawn over the Scene view by ImGuizmo.
    ///
    ///   W / E / R    Move / rotate / scale (while the pointer is over the Scene view)
    ///   Ctrl         Held while dragging: the opposite of the Snap setting (Cmd on macOS)
    ///
    /// The gizmo edits the object's world matrix, which is turned back into local position,
    /// rotation and scale under its parent (Transform::setWorldMatrix).
    class TransformGizmo
    {
    public:
        enum class Operation
        {
            Translate,
            Rotate,
            Scale,
        };

        enum class Space
        {
            World,
            Local,
        };

        /// Must be called once per frame, after ImGui::NewFrame() and before any draw().
        static void beginFrame();

        /// True while the pointer is over a handle or a handle is being dragged, as of the last
        /// draw(). The Scene view then leaves the mouse to the gizmo.
        [[nodiscard]] static bool isActive();

        /// Draws the handles for `object` into the current window, over the rectangle the scene
        /// image covers (screen coordinates, in points), and applies any drag. Returns true if the
        /// object's transform changed.
        bool draw(GameObject& object, const Camera& camera, const ImVec2& viewMin, const ImVec2& viewSize);

        Operation operation = Operation::Translate;
        Space space = Space::World;
        bool snap = false;
        float translateSnap = 0.5f;   // World units.
        float rotateSnap = 15.0f;     // Degrees.
        float scaleSnap = 0.1f;       // Steps of the scale factor.
    };
}

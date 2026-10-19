#pragma once

#include "Math/Aabb.h"
#include "Math/Math.h"
#include "Renderer/Camera.h"

namespace arc
{
    class Input;

    /// Moves a Camera with the mouse and keyboard, in the style of most DCC tools and engine editors.
    ///
    ///   Right drag            Look around (fly mode)
    ///   Right + W A S D       Move; Q / E move down / up; Shift moves 4x faster
    ///   Right + wheel         Change the fly speed
    ///   Alt + left drag       Orbit around the focus point
    ///   Middle drag           Pan
    ///   Alt + Shift + left    Pan, for trackpads without a middle button
    ///   Wheel                 Dolly toward or away from the focus point
    ///   F                     Frame the selection (handled by the Scene panel), or focus on
    ///                         the world origin when nothing is selected
    ///
    /// The focus point sits focusDistance() in front of the camera. Orbiting and dollying move the
    /// camera around it; flying and looking move it with the camera.
    class EditorCamera
    {
    public:
        enum class Mode
        {
            Idle,
            Fly,
            Orbit,
            Pan,
        };

        EditorCamera();

        /// Applies this frame's input. Call only while the scene viewport owns the input. F is left to
        /// the caller, which knows what is selected.
        /// `viewportHeight` is in pixels, like Input's mouse movement, so panning moves the point
        /// under the cursor by exactly as far as the cursor moved.
        void update(const Input& input, float deltaSeconds, float viewportHeight);

        /// Back to the starting view, looking at the origin.
        void reset();

        /// Keeps the focus point and swings the camera around it to look along `direction`.
        void viewAlong(const Vec3& direction);

        /// Keeps the view direction and moves the camera so the bounds fill the view, focused on
        /// their centre. Empty bounds frame a unit-sized region around `fallbackCenter` instead.
        void frame(const Aabb& bounds, const Vec3& fallbackCenter);

        /// Turns toward the world origin without moving, so it becomes the focus point.
        void focusOnOrigin();

        [[nodiscard]] Camera& camera() { return m_camera; }
        [[nodiscard]] const Camera& camera() const { return m_camera; }

        [[nodiscard]] Vec3 focusPoint() const { return m_camera.position + m_camera.forward() * m_focusDistance; }
        [[nodiscard]] float focusDistance() const { return m_focusDistance; }
        [[nodiscard]] Mode mode() const { return m_mode; }

        /// World units per second.
        float flySpeed = 5.0f;
        /// Radians per pixel of mouse movement.
        float lookSensitivity = 0.003f;

    private:
        void rotate(Vec2 mouseDelta);
        void placeAroundFocus(const Vec3& focus);

        Camera m_camera;
        float m_focusDistance = 0.0f;
        Mode m_mode = Mode::Idle;
    };
}

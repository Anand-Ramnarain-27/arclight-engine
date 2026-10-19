#include "Scene/EditorCamera.h"

#include "Platform/Input.h"

#include <algorithm>
#include <cmath>

namespace arc
{
    namespace
    {
        constexpr Vec3 kStartPosition{6.0f, 4.5f, 9.0f};
        constexpr float kMinFocusDistance = 0.05f;
        constexpr float kMinFlySpeed = 0.1f;
        constexpr float kMaxFlySpeed = 500.0f;

        // Each wheel notch scales the distance by this much, so dollying feels the same at any
        // distance: a fixed step would crawl when far away and overshoot when close.
        constexpr float kDollyPerNotch = 0.85f;
        constexpr float kSpeedPerNotch = 1.2f;
        constexpr float kFrameMargin = 1.15f;
        constexpr float kFastMultiplier = 4.0f;
    }

    EditorCamera::EditorCamera()
    {
        reset();
    }

    void EditorCamera::reset()
    {
        m_camera.position = kStartPosition;
        m_camera.lookAlong(-kStartPosition);
        m_focusDistance = length(kStartPosition);
    }

    void EditorCamera::viewAlong(const Vec3& direction)
    {
        const Vec3 focus = focusPoint();
        m_camera.lookAlong(direction);
        placeAroundFocus(focus);
    }

    void EditorCamera::frame(const Aabb& bounds, const Vec3& fallbackCenter)
    {
        const Vec3 center = bounds.isEmpty() ? fallbackCenter : bounds.center();
        const float radius = bounds.isEmpty() ? 0.5f : std::max(length(bounds.size()) * 0.5f, kMinFocusDistance);
        // The distance at which the bounding sphere just fits the vertical field of view, plus a margin.
        m_focusDistance = radius / std::sin(m_camera.verticalFov * 0.5f) * kFrameMargin;
        placeAroundFocus(center);
    }

    void EditorCamera::focusOnOrigin()
    {
        const float distance = length(m_camera.position);
        if (distance < kMinFocusDistance)
        {
            reset();
            return;
        }
        m_camera.lookAlong(-m_camera.position);
        m_focusDistance = distance;
    }

    void EditorCamera::rotate(Vec2 mouseDelta)
    {
        // Mouse right turns right (negative yaw); mouse down looks down. Input's y grows downwards.
        m_camera.yaw -= mouseDelta.x * lookSensitivity;
        m_camera.pitch = std::clamp(m_camera.pitch - mouseDelta.y * lookSensitivity, -Camera::kMaxPitch,
                                    Camera::kMaxPitch);
        m_camera.yaw = std::remainder(m_camera.yaw, kTwoPi);
    }

    void EditorCamera::placeAroundFocus(const Vec3& focus)
    {
        m_camera.position = focus - m_camera.forward() * m_focusDistance;
    }

    void EditorCamera::update(const Input& input, float deltaSeconds, float viewportHeight)
    {
        const bool alt = input.isAltDown();
        const Vec2 mouseDelta = input.mouseDelta();
        const float wheel = input.mouseWheel().y;

        if (input.isDown(MouseButton::Right))
            m_mode = Mode::Fly;
        else if (input.isDown(MouseButton::Middle) || (alt && input.isShiftDown() && input.isDown(MouseButton::Left)))
            m_mode = Mode::Pan;
        else if (alt && input.isDown(MouseButton::Left))
            m_mode = Mode::Orbit;
        else
            m_mode = Mode::Idle;

        switch (m_mode)
        {
            case Mode::Fly:
            {
                rotate(mouseDelta);

                const auto axis = [&input](Key positive, Key negative) {
                    return (input.isDown(positive) ? 1.0f : 0.0f) - (input.isDown(negative) ? 1.0f : 0.0f);
                };
                // Q and E move along world up, not the camera's, so rising never drifts sideways.
                const Vec3 move = m_camera.forward() * axis(Key::W, Key::S) + m_camera.right() * axis(Key::D, Key::A) +
                                  Vec3::up() * axis(Key::E, Key::Q);
                const float speed = flySpeed * (input.isShiftDown() ? kFastMultiplier : 1.0f);
                m_camera.position += normalize(move) * (speed * deltaSeconds);

                if (wheel != 0.0f)
                    flySpeed = std::clamp(flySpeed * std::pow(kSpeedPerNotch, wheel), kMinFlySpeed, kMaxFlySpeed);
                return;
            }
            case Mode::Orbit:
            {
                const Vec3 focus = focusPoint();
                rotate(mouseDelta);
                placeAroundFocus(focus);
                break;
            }
            case Mode::Pan:
            {
                // The height of the view at the focus distance, in world units, over the viewport's
                // height: world units per unit of mouse movement on the focus plane.
                const float viewHeight = 2.0f * m_focusDistance * std::tan(m_camera.verticalFov * 0.5f);
                const float scale = viewportHeight > 0.0f ? viewHeight / viewportHeight : 0.0f;
                m_camera.position += (m_camera.right() * -mouseDelta.x + m_camera.up() * mouseDelta.y) * scale;
                break;
            }
            case Mode::Idle:
                break;
        }

        if (wheel != 0.0f)
        {
            const Vec3 focus = focusPoint();
            m_focusDistance = std::max(m_focusDistance * std::pow(kDollyPerNotch, wheel), kMinFocusDistance);
            placeAroundFocus(focus);
        }
    }
}

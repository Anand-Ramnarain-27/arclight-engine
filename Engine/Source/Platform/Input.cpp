#include "Platform/Input.h"

#include "Core/Log.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cmath>

namespace arc
{
    namespace
    {
        // Key values are cast straight to SDL scancodes, so they must stay identical.
        static_assert(static_cast<int>(Key::A) == SDL_SCANCODE_A);
        static_assert(static_cast<int>(Key::Z) == SDL_SCANCODE_Z);
        static_assert(static_cast<int>(Key::Num1) == SDL_SCANCODE_1);
        static_assert(static_cast<int>(Key::Num0) == SDL_SCANCODE_0);
        static_assert(static_cast<int>(Key::Enter) == SDL_SCANCODE_RETURN);
        static_assert(static_cast<int>(Key::Slash) == SDL_SCANCODE_SLASH);
        static_assert(static_cast<int>(Key::CapsLock) == SDL_SCANCODE_CAPSLOCK);
        static_assert(static_cast<int>(Key::F1) == SDL_SCANCODE_F1);
        static_assert(static_cast<int>(Key::F12) == SDL_SCANCODE_F12);
        static_assert(static_cast<int>(Key::Up) == SDL_SCANCODE_UP);
        static_assert(static_cast<int>(Key::LeftCtrl) == SDL_SCANCODE_LCTRL);
        static_assert(static_cast<int>(Key::RightSuper) == SDL_SCANCODE_RGUI);

        static_assert(static_cast<int>(GamepadButton::South) == SDL_GAMEPAD_BUTTON_SOUTH);
        static_assert(static_cast<int>(GamepadButton::Start) == SDL_GAMEPAD_BUTTON_START);
        static_assert(static_cast<int>(GamepadButton::DpadRight) == SDL_GAMEPAD_BUTTON_DPAD_RIGHT);
        static_assert(SDL_GAMEPAD_AXIS_RIGHT_TRIGGER == 5);

        constexpr size_t index(Key key) { return static_cast<size_t>(key); }
        constexpr size_t index(MouseButton button) { return static_cast<size_t>(button); }
        constexpr size_t index(GamepadButton button) { return static_cast<size_t>(button); }

        // SDL numbers mouse buttons from 1 (SDL_BUTTON_LEFT).
        constexpr bool toMouseButton(uint8_t sdlButton, size_t& out)
        {
            if (sdlButton < SDL_BUTTON_LEFT || sdlButton > SDL_BUTTON_X2)
                return false;
            out = static_cast<size_t>(sdlButton - SDL_BUTTON_LEFT);
            return true;
        }

        // Radial rather than per-axis, so diagonals near the centre are not snapped to an axis.
        // The remaining range is rescaled so output still starts at 0 and reaches 1.
        Float2 applyRadialDeadzone(float x, float y, float deadzone)
        {
            const float length = std::sqrt(x * x + y * y);
            if (length <= deadzone)
                return {};

            const float scaled = std::min((length - deadzone) / (1.0f - deadzone), 1.0f);
            return {x / length * scaled, y / length * scaled};
        }
    }

    bool Input::isDown(Key key) const { return index(key) < kKeyCount && m_keysDown[index(key)]; }
    bool Input::wasPressed(Key key) const { return index(key) < kKeyCount && m_keysPressed[index(key)]; }
    bool Input::wasReleased(Key key) const { return index(key) < kKeyCount && m_keysReleased[index(key)]; }

    bool Input::isShiftDown() const { return isDown(Key::LeftShift) || isDown(Key::RightShift); }
    bool Input::isCtrlDown() const { return isDown(Key::LeftCtrl) || isDown(Key::RightCtrl); }
    bool Input::isAltDown() const { return isDown(Key::LeftAlt) || isDown(Key::RightAlt); }

    bool Input::isDown(MouseButton button) const { return index(button) < kMouseButtonCount && m_mouseDown[index(button)]; }
    bool Input::wasPressed(MouseButton button) const { return index(button) < kMouseButtonCount && m_mousePressed[index(button)]; }
    bool Input::wasReleased(MouseButton button) const { return index(button) < kMouseButtonCount && m_mouseReleased[index(button)]; }

    bool Input::isDown(GamepadButton button) const { return index(button) < kGamepadButtonCount && m_padDown[index(button)]; }
    bool Input::wasPressed(GamepadButton button) const { return index(button) < kGamepadButtonCount && m_padPressed[index(button)]; }
    bool Input::wasReleased(GamepadButton button) const { return index(button) < kGamepadButtonCount && m_padReleased[index(button)]; }

    Float2 Input::leftStick() const
    {
        // SDL reports +y as down; the engine uses +y up.
        return applyRadialDeadzone(m_padAxes[SDL_GAMEPAD_AXIS_LEFTX], -m_padAxes[SDL_GAMEPAD_AXIS_LEFTY], kStickDeadzone);
    }

    Float2 Input::rightStick() const
    {
        return applyRadialDeadzone(m_padAxes[SDL_GAMEPAD_AXIS_RIGHTX], -m_padAxes[SDL_GAMEPAD_AXIS_RIGHTY], kStickDeadzone);
    }

    float Input::leftTrigger() const
    {
        const float value = m_padAxes[SDL_GAMEPAD_AXIS_LEFT_TRIGGER];
        return value <= kTriggerDeadzone ? 0.0f : (value - kTriggerDeadzone) / (1.0f - kTriggerDeadzone);
    }

    float Input::rightTrigger() const
    {
        const float value = m_padAxes[SDL_GAMEPAD_AXIS_RIGHT_TRIGGER];
        return value <= kTriggerDeadzone ? 0.0f : (value - kTriggerDeadzone) / (1.0f - kTriggerDeadzone);
    }

    void Input::beginFrame()
    {
        m_keysPressed.reset();
        m_keysReleased.reset();
        m_mousePressed.reset();
        m_mouseReleased.reset();
        m_padPressed.reset();
        m_padReleased.reset();
        m_mouseDelta = {};
        m_mouseWheel = {};
    }

    void Input::handleEvent(const SDL_Event& event, SDL_Window* window)
    {
        // SDL reports mouse coordinates in points; the renderer and picking work in pixels.
        const float pixelDensity = window ? SDL_GetWindowPixelDensity(window) : 1.0f;

        switch (event.type)
        {
            case SDL_EVENT_KEY_DOWN:
            case SDL_EVENT_KEY_UP:
            {
                const auto code = static_cast<size_t>(event.key.scancode);
                if (code >= kKeyCount || event.key.repeat)
                    break;
                if (event.key.down && !m_keysDown[code])
                    m_keysPressed.set(code);
                if (!event.key.down && m_keysDown[code])
                    m_keysReleased.set(code);
                m_keysDown.set(code, event.key.down);
                break;
            }

            case SDL_EVENT_MOUSE_BUTTON_DOWN:
            case SDL_EVENT_MOUSE_BUTTON_UP:
            {
                size_t button = 0;
                if (!toMouseButton(event.button.button, button))
                    break;
                if (event.button.down)
                    m_mousePressed.set(button);
                else
                    m_mouseReleased.set(button);
                m_mouseDown.set(button, event.button.down);
                break;
            }

            case SDL_EVENT_MOUSE_MOTION:
                m_mousePosition = {event.motion.x * pixelDensity, event.motion.y * pixelDensity};
                m_mouseDelta.x += event.motion.xrel * pixelDensity;
                m_mouseDelta.y += event.motion.yrel * pixelDensity;
                break;

            case SDL_EVENT_MOUSE_WHEEL:
            {
                // Natural scrolling (default on macOS) reports a flipped direction.
                const float sign = event.wheel.direction == SDL_MOUSEWHEEL_FLIPPED ? -1.0f : 1.0f;
                m_mouseWheel.x += event.wheel.x * sign;
                m_mouseWheel.y += event.wheel.y * sign;
                break;
            }

            // Key-up events are not delivered to an unfocused window, so held keys would stay down.
            case SDL_EVENT_WINDOW_FOCUS_LOST:
                releaseAll();
                break;

            case SDL_EVENT_GAMEPAD_ADDED:
                if (!m_gamepad)
                    openFirstGamepad();
                break;

            case SDL_EVENT_GAMEPAD_REMOVED:
                if (m_gamepad && event.gdevice.which == m_gamepadId)
                {
                    ARC_LOG_INFO("Gamepad disconnected: {}", m_gamepadName);
                    closeGamepad();
                    openFirstGamepad();
                }
                break;

            case SDL_EVENT_GAMEPAD_BUTTON_DOWN:
            case SDL_EVENT_GAMEPAD_BUTTON_UP:
            {
                if (event.gbutton.which != m_gamepadId || event.gbutton.button >= kGamepadButtonCount)
                    break;
                const size_t button = event.gbutton.button;
                if (event.gbutton.down)
                    m_padPressed.set(button);
                else
                    m_padReleased.set(button);
                m_padDown.set(button, event.gbutton.down);
                break;
            }

            case SDL_EVENT_GAMEPAD_AXIS_MOTION:
            {
                if (event.gaxis.which != m_gamepadId || event.gaxis.axis >= m_padAxes.size())
                    break;
                // Sticks span [-32768, 32767], so normalise by 32767 and clamp the extra negative step.
                m_padAxes[event.gaxis.axis] = std::clamp(event.gaxis.value / 32767.0f, -1.0f, 1.0f);
                break;
            }

            default:
                break;
        }
    }

    void Input::shutdown()
    {
        closeGamepad();
    }

    void Input::releaseAll()
    {
        m_keysReleased |= m_keysDown;
        m_keysDown.reset();
        m_mouseReleased |= m_mouseDown;
        m_mouseDown.reset();
    }

    void Input::openFirstGamepad()
    {
        int count = 0;
        SDL_JoystickID* ids = SDL_GetGamepads(&count);
        if (!ids)
            return;

        for (int i = 0; i < count && !m_gamepad; ++i)
        {
            m_gamepad = SDL_OpenGamepad(ids[i]);
            if (m_gamepad)
            {
                m_gamepadId = ids[i];
                const char* name = SDL_GetGamepadName(m_gamepad);
                m_gamepadName = name ? name : "Unknown gamepad";
                ARC_LOG_INFO("Gamepad connected: {}", m_gamepadName);
            }
        }
        SDL_free(ids);
    }

    void Input::closeGamepad()
    {
        if (m_gamepad)
            SDL_CloseGamepad(m_gamepad);

        m_gamepad = nullptr;
        m_gamepadId = 0;
        m_gamepadName.clear();
        m_padReleased |= m_padDown;
        m_padDown.reset();
        m_padAxes.fill(0.0f);
    }
}

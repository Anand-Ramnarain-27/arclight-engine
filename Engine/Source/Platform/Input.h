#pragma once

#include <array>
#include <bitset>
#include <cstdint>
#include <string>

union SDL_Event;
struct SDL_Gamepad;
struct SDL_Window;

namespace arc
{
    /// Physical key positions, not characters: Key::W is the same key on QWERTY and AZERTY.
    /// Values are USB HID keyboard usage IDs, which SDL scancodes also use.
    enum class Key : uint16_t
    {
        Unknown = 0,

        A = 4, B, C, D, E, F, G, H, I, J, K, L, M,
        N, O, P, Q, R, S, T, U, V, W, X, Y, Z,

        Num1 = 30, Num2, Num3, Num4, Num5, Num6, Num7, Num8, Num9, Num0,

        Enter = 40,
        Escape = 41,
        Backspace = 42,
        Tab = 43,
        Space = 44,
        Minus = 45,
        Equals = 46,
        LeftBracket = 47,
        RightBracket = 48,
        Backslash = 49,
        Semicolon = 51,
        Apostrophe = 52,
        Grave = 53,
        Comma = 54,
        Period = 55,
        Slash = 56,
        CapsLock = 57,

        F1 = 58, F2, F3, F4, F5, F6, F7, F8, F9, F10, F11, F12,

        PrintScreen = 70,
        ScrollLock = 71,
        Pause = 72,
        Insert = 73,
        Home = 74,
        PageUp = 75,
        Delete = 76,
        End = 77,
        PageDown = 78,
        Right = 79,
        Left = 80,
        Down = 81,
        Up = 82,

        LeftCtrl = 224,
        LeftShift = 225,
        LeftAlt = 226,
        LeftSuper = 227,   // Windows key / Command.
        RightCtrl = 228,
        RightShift = 229,
        RightAlt = 230,
        RightSuper = 231,
    };

    enum class MouseButton : uint8_t
    {
        Left,
        Middle,
        Right,
        X1,
        X2,
        Count,
    };

    /// Named by position, not label: South is A on Xbox and Cross on PlayStation.
    enum class GamepadButton : uint8_t
    {
        South,
        East,
        West,
        North,
        Back,
        Guide,
        Start,
        LeftStick,
        RightStick,
        LeftShoulder,
        RightShoulder,
        DpadUp,
        DpadDown,
        DpadLeft,
        DpadRight,
        Count,
    };

    // TODO: Replace with the engine math type once the math library exists.
    struct Float2
    {
        float x = 0.0f;
        float y = 0.0f;
    };

    /// Keyboard, mouse and gamepad state for the current frame.
    ///
    /// "Pressed" and "Released" are true only on the frame the transition happened.
    /// They are recorded per event, so a press and release within one frame are both seen.
    class Input
    {
    public:
        static constexpr float kStickDeadzone = 0.15f;
        static constexpr float kTriggerDeadzone = 0.05f;

        Input() = default;

        Input(const Input&) = delete;
        Input& operator=(const Input&) = delete;

        [[nodiscard]] bool isDown(Key key) const;
        [[nodiscard]] bool wasPressed(Key key) const;
        [[nodiscard]] bool wasReleased(Key key) const;

        /// Either the left or right modifier key.
        [[nodiscard]] bool isShiftDown() const;
        [[nodiscard]] bool isCtrlDown() const;
        [[nodiscard]] bool isAltDown() const;

        [[nodiscard]] bool isDown(MouseButton button) const;
        [[nodiscard]] bool wasPressed(MouseButton button) const;
        [[nodiscard]] bool wasReleased(MouseButton button) const;

        /// Pixels from the window's top-left corner.
        [[nodiscard]] Float2 mousePosition() const { return m_mousePosition; }
        /// Pixels moved this frame.
        [[nodiscard]] Float2 mouseDelta() const { return m_mouseDelta; }
        /// Positive y scrolls away from the user. Notches this frame; may be fractional on trackpads.
        [[nodiscard]] Float2 mouseWheel() const { return m_mouseWheel; }

        [[nodiscard]] bool hasGamepad() const { return m_gamepad != nullptr; }
        [[nodiscard]] const std::string& gamepadName() const { return m_gamepadName; }
        [[nodiscard]] bool isDown(GamepadButton button) const;
        [[nodiscard]] bool wasPressed(GamepadButton button) const;
        [[nodiscard]] bool wasReleased(GamepadButton button) const;

        /// Each axis in [-1, 1] after the radial deadzone; +y is up.
        [[nodiscard]] Float2 leftStick() const;
        [[nodiscard]] Float2 rightStick() const;
        /// [0, 1] after the deadzone.
        [[nodiscard]] float leftTrigger() const;
        [[nodiscard]] float rightTrigger() const;

        /// Engine-internal. Called by the Application before events are pumped each frame.
        void beginFrame();
        /// Engine-internal. Called by the Application for every SDL event.
        void handleEvent(const SDL_Event& event, SDL_Window* window);
        /// Engine-internal. Closes the open gamepad; must run before SDL_Quit().
        void shutdown();

    private:
        static constexpr size_t kKeyCount = 512;
        static constexpr size_t kMouseButtonCount = static_cast<size_t>(MouseButton::Count);
        static constexpr size_t kGamepadButtonCount = static_cast<size_t>(GamepadButton::Count);

        void releaseAll();
        void openFirstGamepad();
        void closeGamepad();

        std::bitset<kKeyCount> m_keysDown;
        std::bitset<kKeyCount> m_keysPressed;
        std::bitset<kKeyCount> m_keysReleased;

        std::bitset<kMouseButtonCount> m_mouseDown;
        std::bitset<kMouseButtonCount> m_mousePressed;
        std::bitset<kMouseButtonCount> m_mouseReleased;
        Float2 m_mousePosition;
        Float2 m_mouseDelta;
        Float2 m_mouseWheel;

        SDL_Gamepad* m_gamepad = nullptr;
        uint32_t m_gamepadId = 0;
        std::string m_gamepadName;
        std::bitset<kGamepadButtonCount> m_padDown;
        std::bitset<kGamepadButtonCount> m_padPressed;
        std::bitset<kGamepadButtonCount> m_padReleased;
        std::array<float, 6> m_padAxes{};
    };
}

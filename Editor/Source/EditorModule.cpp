#include "EditorModule.h"

#include "Application/Application.h"
#include "Core/Log.h"

#include <format>

namespace arc
{
    void EditorModule::update(const FrameTime& time)
    {
        handlePlayControls();
        logInputActivity();

        // Driven by real time so the title keeps refreshing while the game is paused.
        if (time.realTime >= m_nextTitleRefresh)
        {
            refreshTitle(time);
            m_nextTitleRefresh = time.realTime + kTitleRefreshSeconds;
        }
    }

    void EditorModule::handlePlayControls()
    {
        const Input& input = app().input();
        GameClock& clock = app().gameClock();

        if (input.wasPressed(Key::F5) || input.wasPressed(GamepadButton::Start))
        {
            if (input.isShiftDown())
                clock.stop();
            else if (clock.state() == GameClock::State::Playing)
                clock.pause();
            else
                clock.play();
            ARC_LOG_INFO("Game clock: {}", toString(clock.state()));
        }

        if (input.wasPressed(Key::F6))
            clock.step();

        // Equals is the unshifted '+' key on US and most European layouts.
        if (input.wasPressed(Key::Equals))
            clock.setTimeScale(clock.timeScale() * 2.0f);
        if (input.wasPressed(Key::Minus))
            clock.setTimeScale(clock.timeScale() * 0.5f);
        if (input.wasPressed(Key::Num0))
            clock.setTimeScale(1.0f);
    }

    void EditorModule::logInputActivity()
    {
        const Input& input = app().input();

        if (input.wasPressed(MouseButton::Left))
        {
            const Float2 position = input.mousePosition();
            ARC_LOG_TRACE("Left click at ({:.0f}, {:.0f}) px", position.x, position.y);
        }

        if (const Float2 wheel = input.mouseWheel(); wheel.y != 0.0f)
            ARC_LOG_TRACE("Mouse wheel {:+.2f}", wheel.y);

        if (input.wasPressed(GamepadButton::South))
        {
            const Float2 stick = input.leftStick();
            ARC_LOG_TRACE("Gamepad South pressed; left stick ({:+.2f}, {:+.2f})", stick.x, stick.y);
        }
    }

    void EditorModule::refreshTitle(const FrameTime& time)
    {
        const FrameStats& stats = app().frameStats();
        const GameClock& clock = app().gameClock();

        app().window().setTitle(std::format("Arclight  |  {:.0f} FPS ({:.2f} ms)  |  {} x{:g}  |  game {:.2f} s",
                                            stats.averageFps(), stats.averageMs(),
                                            toString(clock.state()), clock.timeScale(), time.gameTime));
    }
}

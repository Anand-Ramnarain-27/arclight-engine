#pragma once

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>

namespace arc
{
    // steady_clock is monotonic. system_clock follows the wall clock and can jump
    // backwards or forwards when the OS adjusts the time, producing negative or huge deltas.
    using Clock = std::chrono::steady_clock;
    using Seconds = std::chrono::duration<double>;
    using Milliseconds = std::chrono::duration<double, std::milli>;

    /// Measures elapsed time. Starts running on construction.
    class Stopwatch
    {
    public:
        Stopwatch() { start(); }

        /// Restarts from zero.
        void start();

        /// Freezes the reading until start() is called again.
        void stop();

        [[nodiscard]] Clock::duration elapsed() const;
        [[nodiscard]] double elapsedMs() const { return Milliseconds(elapsed()).count(); }
        [[nodiscard]] double elapsedSeconds() const { return Seconds(elapsed()).count(); }
        [[nodiscard]] bool isRunning() const { return m_running; }

    private:
        Clock::time_point m_start;
        Clock::time_point m_stop;
        bool m_running = false;
    };

    /// Timing for one frame, passed to every Module::update(). All times are in seconds.
    struct FrameTime
    {
        uint64_t frameIndex = 0;

        // Real clock: always advances, unaffected by Play/Pause or time scale.
        // Use for the editor, UI, camera fly-through and profiling.
        double realTime = 0.0;
        float realDelta = 0.0f;

        // Game clock: the simulated world. Zero delta while stopped or paused.
        // Use for gameplay, physics, animation, particles and audio.
        double gameTime = 0.0;
        float gameDelta = 0.0f;
        float timeScale = 1.0f;
    };

    /// The editor's Play / Pause / Stop / Step controls and the game time scale.
    class GameClock
    {
    public:
        enum class State
        {
            Stopped,
            Playing,
            Paused,
        };

        static constexpr float kMinTimeScale = 0.0f;
        static constexpr float kMaxTimeScale = 8.0f;

        // A single game step never exceeds this, so a breakpoint or a load hitch does not
        // make physics and animation jump forward by seconds in one frame.
        static constexpr float kMaxGameDelta = 0.25f;

        /// From Stopped, game time restarts at zero.
        void play();
        void pause();
        void stop();

        /// Advances the game by exactly one frame on the next tick(), then pauses.
        void step();

        /// Clamped to [kMinTimeScale, kMaxTimeScale].
        void setTimeScale(float scale);

        [[nodiscard]] State state() const { return m_state; }
        [[nodiscard]] static const char* stateName(State state);
        [[nodiscard]] float timeScale() const { return m_timeScale; }

        /// Called once per frame by the Application with the real time since the previous frame.
        [[nodiscard]] FrameTime tick(Clock::duration realDelta);

    private:
        State m_state = State::Stopped;
        float m_timeScale = 1.0f;
        bool m_stepRequested = false;

        uint64_t m_frameIndex = 0;
        double m_realTime = 0.0;
        double m_gameTime = 0.0;
    };

    /// Rolling frame-time history for FPS readouts and graphs.
    class FrameStats
    {
    public:
        static constexpr size_t kHistorySize = 120;

        void addFrame(float frameMs);

        /// Mean over the history window, which smooths the readout.
        [[nodiscard]] float averageMs() const;
        [[nodiscard]] float averageFps() const;

        /// Oldest sample first is at index offset(); the buffer wraps.
        [[nodiscard]] const std::array<float, kHistorySize>& history() const { return m_history; }
        [[nodiscard]] size_t offset() const { return m_next; }

    private:
        std::array<float, kHistorySize> m_history{};
        size_t m_next = 0;
        size_t m_count = 0;
        double m_sum = 0.0;
    };
}

#include "Core/Time.h"

#include <algorithm>

namespace arc
{
    void Stopwatch::start()
    {
        m_start = Clock::now();
        m_running = true;
    }

    void Stopwatch::stop()
    {
        if (m_running)
        {
            m_stop = Clock::now();
            m_running = false;
        }
    }

    Clock::duration Stopwatch::elapsed() const
    {
        return (m_running ? Clock::now() : m_stop) - m_start;
    }

    void GameClock::play()
    {
        if (m_state == State::Stopped)
            m_gameTime = 0.0;
        m_state = State::Playing;
        m_stepRequested = false;
    }

    void GameClock::pause()
    {
        if (m_state == State::Playing)
            m_state = State::Paused;
    }

    void GameClock::stop()
    {
        m_state = State::Stopped;
        m_gameTime = 0.0;
        m_stepRequested = false;
    }

    void GameClock::step()
    {
        if (m_state != State::Playing)
            m_stepRequested = true;
    }

    void GameClock::setTimeScale(float scale)
    {
        m_timeScale = std::clamp(scale, kMinTimeScale, kMaxTimeScale);
    }

    FrameTime GameClock::tick(Clock::duration realDelta)
    {
        const auto realSeconds = static_cast<float>(Seconds(realDelta).count());
        m_realTime += realSeconds;

        const bool advanceGame = m_state == State::Playing || m_stepRequested;
        const float gameDelta = advanceGame ? std::min(realSeconds, kMaxGameDelta) * m_timeScale : 0.0f;
        m_gameTime += gameDelta;

        if (m_stepRequested)
        {
            m_stepRequested = false;
            m_state = State::Paused;
        }

        return FrameTime{
            .frameIndex = m_frameIndex++,
            .realTime = m_realTime,
            .realDelta = realSeconds,
            .gameTime = m_gameTime,
            .gameDelta = gameDelta,
            .timeScale = m_timeScale,
        };
    }

    void FrameStats::addFrame(float frameMs)
    {
        if (m_count == kHistorySize)
            m_sum -= m_history[m_next];
        else
            ++m_count;

        m_history[m_next] = frameMs;
        m_sum += frameMs;
        m_next = (m_next + 1) % kHistorySize;
    }

    float FrameStats::averageMs() const
    {
        return m_count == 0 ? 0.0f : static_cast<float>(m_sum / static_cast<double>(m_count));
    }

    float FrameStats::averageFps() const
    {
        const float ms = averageMs();
        return ms > 0.0f ? 1000.0f / ms : 0.0f;
    }

    const char* toString(GameClock::State state)
    {
        switch (state)
        {
            case GameClock::State::Stopped: return "Stopped";
            case GameClock::State::Playing: return "Playing";
            case GameClock::State::Paused:  return "Paused";
        }
        return "Unknown";
    }
}

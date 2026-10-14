#include "Core/Time.h"

#include <doctest/doctest.h>

using namespace arc;
using namespace std::chrono_literals;

TEST_CASE("Game clock is stopped by default and only real time advances")
{
    GameClock clock;
    const FrameTime t = clock.tick(16ms);
    CHECK(t.gameDelta == 0.0f);
    CHECK(t.realDelta == doctest::Approx(0.016f));
}

TEST_CASE("Game clock play, pause, step and stop")
{
    GameClock clock;
    clock.play();
    CHECK(clock.tick(16ms).gameDelta == doctest::Approx(0.016f));

    clock.pause();
    CHECK(clock.tick(16ms).gameDelta == 0.0f);

    clock.step();
    CHECK(clock.tick(16ms).gameDelta == doctest::Approx(0.016f));
    CHECK(clock.state() == GameClock::State::Paused);
    CHECK(clock.tick(16ms).gameDelta == 0.0f);

    clock.stop();
    CHECK(clock.tick(16ms).gameTime == 0.0);
}

TEST_CASE("Game delta is clamped but real delta is not")
{
    GameClock clock;
    clock.play();
    const FrameTime t = clock.tick(3s);
    CHECK(t.gameDelta == GameClock::kMaxGameDelta);
    CHECK(t.realDelta == doctest::Approx(3.0f));
}

TEST_CASE("Time scale is applied and clamped")
{
    GameClock clock;
    clock.play();
    clock.setTimeScale(2.0f);
    CHECK(clock.tick(10ms).gameDelta == doctest::Approx(0.02f));
    clock.setTimeScale(100.0f);
    CHECK(clock.timeScale() == GameClock::kMaxTimeScale);
    clock.setTimeScale(-1.0f);
    CHECK(clock.timeScale() == GameClock::kMinTimeScale);
}

TEST_CASE("Frame stats average over the history window")
{
    FrameStats stats;
    for (int i = 0; i < 200; ++i)
        stats.addFrame(i < 100 ? 10.0f : 20.0f);

    // The window holds the last 120 frames: 20 at 10 ms and 100 at 20 ms.
    CHECK(stats.averageMs() == doctest::Approx((20 * 10.0f + 100 * 20.0f) / 120.0f));
}

TEST_CASE("Stopwatch reading freezes when stopped")
{
    Stopwatch watch;
    watch.stop();
    const double first = watch.elapsedMs();
    CHECK(watch.elapsedMs() == first);
    CHECK_FALSE(watch.isRunning());
}

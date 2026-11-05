#include "Core/Profiler.h"
#include "RHI/GpuTiming.h"

#include <doctest/doctest.h>

#include <array>
#include <string>
#include <thread>
#include <vector>

using namespace arc;

namespace
{
    // The recorder keeps the zones of the thread that called setMainThread(), so each test gives it a
    // thread of its own, which ends with the test.
    profiler::CpuFrame recordFrames(int frames, void (*body)())
    {
        profiler::CpuFrame result;
        std::thread thread([&] {
            profiler::setMainThread();
            profiler::frameMark();
            for (int i = 0; i < frames; ++i)
            {
                body();
                profiler::frameMark();
            }
            result = profiler::lastCpuFrame();
        });
        thread.join();
        return result;
    }
}

TEST_CASE("CPU zones are recorded per frame in the order they began, with their depth")
{
    const profiler::CpuFrame frame = recordFrames(2, [] {
        ARC_PROFILE_SCOPE("Outer");
        {
            ARC_PROFILE_SCOPE("First child");
        }
        {
            ARC_PROFILE_SCOPE("Second child");
            ARC_PROFILE_SCOPE_NAMED("Named", std::string_view("Grandchild"));
        }
    });

    REQUIRE(frame.zones.size() == 4);
    CHECK(frame.zones[0].name == "Outer");
    CHECK(frame.zones[0].depth == 0);
    CHECK(frame.zones[1].name == "First child");
    CHECK(frame.zones[1].depth == 1);
    CHECK(frame.zones[2].name == "Second child");
    CHECK(frame.zones[3].name == "Grandchild");
    CHECK(frame.zones[3].depth == 2);

    for (const profiler::CpuZone& zone : frame.zones)
    {
        CHECK(zone.endNs >= zone.startNs);
        CHECK(zone.startNs >= 0);
        CHECK(zone.endNs <= frame.durationNs);
    }
    // Children lie inside their parent.
    CHECK(frame.zones[1].startNs >= frame.zones[0].startNs);
    CHECK(frame.zones[2].endNs <= frame.zones[0].endNs);
}

TEST_CASE("Each frame starts with no zones and a new index")
{
    const profiler::CpuFrame first = recordFrames(3, [] { ARC_PROFILE_SCOPE("Only"); });
    const profiler::CpuFrame second = recordFrames(3, [] { ARC_PROFILE_SCOPE("Only"); });
    CHECK(second.zones.size() == 1);
    // recordFrames() marks one frame before its three.
    CHECK(second.index == first.index + 4);
}

TEST_CASE("Zones on other threads are not recorded in the main thread's frame")
{
    const profiler::CpuFrame frame = recordFrames(1, [] {
        ARC_PROFILE_SCOPE("Main");
        std::thread worker([] { ARC_PROFILE_SCOPE("Worker"); });
        worker.join();
    });
    REQUIRE(frame.zones.size() == 1);
    CHECK(frame.zones[0].name == "Main");
}

TEST_CASE("GPU timestamp ticks convert to nanoseconds without overflow")
{
    CHECK(rhi::ticksToNanoseconds(0, 10'000'000) == 0);
    CHECK(rhi::ticksToNanoseconds(10'000'000, 10'000'000) == 1'000'000'000);
    CHECK(rhi::ticksToNanoseconds(1, 10'000'000) == 100);
    // 2^50 ticks at 24 MHz: multiplying by 10^9 first would overflow 64 bits.
    CHECK(rhi::ticksToNanoseconds(uint64_t(1) << 50, 24'000'000) == 46912496118442666);
}

TEST_CASE("GPU pass timings are built from two timestamps per pass")
{
    rhi::GpuTimingRecorder recorder;
    recorder.init(rhi::Backend::Null, true, 0);
    REQUIRE(recorder.supported());

    recorder.beginFrame(1, 42);
    const uint32_t scene = recorder.beginPass("Scene");
    recorder.endPass(scene);
    const uint32_t empty = recorder.beginPass("Empty");
    recorder.endPass(empty);
    const uint32_t ui = recorder.beginPass("ImGui");
    recorder.endPass(ui);
    CHECK(scene == 0);
    CHECK(ui == 2);
    REQUIRE(recorder.endFrame() == 3);

    // Nanoseconds. The second pass wrote no timestamps; the third overlaps the first, as passes do on
    // tile-based GPUs.
    const std::array<int64_t, 6> timestamps{1'000'000, 3'000'000, 0, 0, 2'500'000, 9'000'000};
    recorder.resolve(1, timestamps);

    const rhi::GpuFrameTimings& frame = recorder.latest();
    REQUIRE(frame.valid);
    CHECK(frame.frameNumber == 42);
    CHECK(frame.frameMs == doctest::Approx(8.0));
    REQUIRE(frame.passes.size() == 3);
    CHECK(frame.passes[0].name == "Scene");
    CHECK(frame.passes[0].valid);
    CHECK(frame.passes[0].startMs == doctest::Approx(0.0));
    CHECK(frame.passes[0].durationMs == doctest::Approx(2.0));
    CHECK_FALSE(frame.passes[1].valid);
    CHECK(frame.passes[2].startMs == doctest::Approx(1.5));
    CHECK(frame.passes[2].durationMs == doctest::Approx(6.5));
}

TEST_CASE("GPU timing does nothing when unsupported or disabled")
{
    rhi::GpuTimingRecorder unsupported;
    unsupported.init(rhi::Backend::Null, false, 0);
    unsupported.beginFrame(0, 1);
    CHECK(unsupported.beginPass("Pass") == rhi::GpuTimingRecorder::kNoPass);
    CHECK(unsupported.endFrame() == 0);

    rhi::GpuTimingRecorder disabled;
    disabled.init(rhi::Backend::Null, true, 0);
    disabled.setEnabled(false);
    disabled.beginFrame(0, 1);
    CHECK(disabled.beginPass("Pass") == rhi::GpuTimingRecorder::kNoPass);
    CHECK(disabled.endFrame() == 0);
    CHECK_FALSE(disabled.latest().valid);
}

TEST_CASE("A frame times at most kMaxPassesPerFrame passes")
{
    rhi::GpuTimingRecorder recorder;
    recorder.init(rhi::Backend::Null, true, 0);
    recorder.beginFrame(0, 1);
    for (uint32_t i = 0; i < rhi::GpuTimingRecorder::kMaxPassesPerFrame; ++i)
        recorder.endPass(recorder.beginPass("Pass"));
    CHECK(recorder.beginPass("One too many") == rhi::GpuTimingRecorder::kNoPass);
    CHECK(recorder.endFrame() == rhi::GpuTimingRecorder::kMaxPassesPerFrame);

    std::vector<int64_t> timestamps(rhi::GpuTimingRecorder::kTimestampsPerFrame);
    for (size_t i = 0; i < timestamps.size(); ++i)
        timestamps[i] = static_cast<int64_t>(1000 + i * 10);
    recorder.resolve(0, timestamps);
    CHECK(recorder.latest().passes.size() == rhi::GpuTimingRecorder::kMaxPassesPerFrame);
}

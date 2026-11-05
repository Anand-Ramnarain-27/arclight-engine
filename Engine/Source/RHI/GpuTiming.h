#pragma once

#include "RHI/RHITypes.h"

#include <array>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace arc::rhi
{
    /// How long one render pass took on the GPU.
    struct GpuPassTiming
    {
        std::string name;
        /// Milliseconds after the first pass of the frame began.
        double startMs = 0.0;
        double durationMs = 0.0;
        /// False when the GPU wrote no timestamp for the pass; startMs and durationMs are then 0.
        bool valid = false;
    };

    /// The GPU timings of one frame, available kMaxFramesInFlight frames after it was recorded.
    struct GpuFrameTimings
    {
        uint64_t frameNumber = 0;
        /// From the start of the first timed pass to the end of the last. Copies outside passes,
        /// and the time the GPU waits on the display, are not included.
        double frameMs = 0.0;
        std::vector<GpuPassTiming> passes;
        /// False until a frame with timestamps has completed.
        bool valid = false;
    };

    /// Converts timestamp ticks at `frequency` ticks per second to nanoseconds without the overflow a
    /// multiply by 10^9 would give for ticks above 2^34. `frequency` must be below 1.8 x 10^10.
    [[nodiscard]] constexpr int64_t ticksToNanoseconds(uint64_t ticks, uint64_t frequency)
    {
        const uint64_t seconds = ticks / frequency;
        const uint64_t remainder = ticks % frequency;
        return static_cast<int64_t>(seconds * 1'000'000'000ull + remainder * 1'000'000'000ull / frequency);
    }

    /// The backend-independent half of per-pass GPU timing. Each backend writes two timestamps per
    /// render pass of a frame, a start and an end, into the frame slot's region of its query memory,
    /// and hands them back here in nanoseconds once the GPU has finished the frame. Owned by the
    /// device. Passes are what every backend can time: Apple GPUs only sample counters at the
    /// boundaries of a pass, not between its draws.
    ///
    /// When built with Tracy, each timed pass is also a zone of a GPU context in Tracy.
    ///
    /// Per frame, on the thread that records it:
    ///   resolve(slot, ...)    once the GPU has finished the frame that last used the slot
    ///   syncClock(gpuNowNs)   only if startTracySession() returned true
    ///   beginFrame(slot, n)
    ///   beginPass() / endPass() for each render pass
    ///   endFrame()            returns how many passes' timestamps to resolve
    class GpuTimingRecorder
    {
    public:
        static constexpr uint32_t kMaxPassesPerFrame = 128;
        static constexpr uint32_t kTimestampsPerFrame = kMaxPassesPerFrame * 2;
        static constexpr uint32_t kNoPass = ~0u;

        GpuTimingRecorder() = default;
        ~GpuTimingRecorder();

        GpuTimingRecorder(const GpuTimingRecorder&) = delete;
        GpuTimingRecorder& operator=(const GpuTimingRecorder&) = delete;

        /// `gpuNowNs` is the GPU clock's current reading, which lines the GPU zones up with the CPU
        /// ones in Tracy. Without init(), or with `supported` false, every call does nothing.
        void init(Backend backend, bool supported, int64_t gpuNowNs);

        [[nodiscard]] bool supported() const { return m_supported; }

        /// Takes effect from the next frame.
        void setEnabled(bool enabled) { m_enabled = enabled; }
        [[nodiscard]] bool enabled() const { return m_enabled; }

        /// Hands over the timestamps of the frame that last used `slot`: two per pass, in the order
        /// the passes began, in nanoseconds of the GPU clock, with 0 for a timestamp that was not
        /// written. Must cover the count endFrame() returned for that frame.
        void resolve(uint32_t slot, std::span<const int64_t> timestampsNs);

        /// Checks whether a Tracy profiler has connected since the last frame. If so, returns true,
        /// and the backend reads the GPU clock and passes it to syncClock() before beginFrame(), so the
        /// GPU zones line up with the CPU ones despite clock drift since start-up.
        [[nodiscard]] bool startTracySession();
        void syncClock(int64_t gpuNowNs);

        void beginFrame(uint32_t slot, uint64_t frameNumber);

        /// The pass's index within the frame, whose timestamps go at 2 * index and 2 * index + 1 of the
        /// slot's region, or kNoPass when timing is off or the frame has kMaxPassesPerFrame passes.
        [[nodiscard]] uint32_t beginPass(std::string_view name);
        void endPass(uint32_t pass);

        /// Returns the number of passes to resolve for this frame.
        [[nodiscard]] uint32_t endFrame();

        /// The newest frame resolved.
        [[nodiscard]] const GpuFrameTimings& latest() const { return m_latest; }

    private:
        struct Slot
        {
            std::vector<std::string> names;
            uint64_t frameNumber = 0;
            uint32_t passCount = 0;
            uint32_t tracySession = 0;   // 0: the frame sent no zones to Tracy.
        };

        [[nodiscard]] uint16_t tracyQueryId(uint32_t slot, uint32_t pass, bool end) const;
        void sendToTracy(const Slot& slot, uint32_t slotIndex, std::span<const int64_t> timestampsNs) const;

        std::array<Slot, kMaxFramesInFlight> m_slots;
        GpuFrameTimings m_latest;
        Backend m_backend = Backend::Null;
        uint32_t m_currentSlot = 0;
        bool m_supported = false;
        bool m_enabled = true;
        bool m_recording = false;

        // Tracy sessions: bumped each time a profiler connects, so zones begun while none was
        // connected are never completed in a later session.
        uint32_t m_tracySession = 0;
        uint32_t m_tracySyncedSession = 0;
        bool m_tracyConnected = false;
        bool m_tracyContextCreated = false;
    };
}

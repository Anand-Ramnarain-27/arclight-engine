#include "RHI/GpuTiming.h"

#include "Core/Assert.h"

#include <algorithm>
#include <limits>

#if ARC_TRACY
    #include <tracy/TracyC.h>
#endif

namespace arc::rhi
{
    namespace
    {
#if ARC_TRACY
        // Arclight has one GPU queue, so one Tracy GPU context. The C API leaves context ids to the caller.
        constexpr uint8_t kTracyContext = 0;

        // tracy::GpuContextType, which only the C++ API declares.
        uint8_t tracyContextType(Backend backend)
        {
            switch (backend)
            {
                case Backend::D3D12: return 4;
                case Backend::Metal: return 6;
                case Backend::Null:  return 7;
            }
            return 7;
        }

        constexpr char kTracySourceFile[] = __FILE__;
        constexpr char kTracyFunction[] = "Render pass";
#endif

        bool validPair(int64_t start, int64_t end)
        {
            return start > 0 && end > 0 && end >= start;
        }

        double toMs(int64_t nanoseconds)
        {
            return static_cast<double>(nanoseconds) * 1e-6;
        }
    }

    GpuTimingRecorder::~GpuTimingRecorder() = default;

    void GpuTimingRecorder::init(Backend backend, bool supported, int64_t gpuNowNs)
    {
        m_backend = backend;
        m_supported = supported;
        if (!m_supported)
            return;

#if ARC_TRACY
        // Kept by Tracy and sent to every profiler that connects later, since the client is on demand.
        ___tracy_gpu_new_context_data context{};
        context.gpuTime = gpuNowNs;
        context.period = 1.0f;   // Timestamps are handed over in nanoseconds.
        context.context = kTracyContext;
        context.flags = 0;
        context.type = tracyContextType(backend);
        ___tracy_emit_gpu_new_context_serial(context);

        const std::string_view name = backendName(backend);
        ___tracy_emit_gpu_context_name_serial({kTracyContext, name.data(), static_cast<uint16_t>(name.size())});
        m_tracyContextCreated = true;
#else
        (void)gpuNowNs;
#endif
    }

    bool GpuTimingRecorder::startTracySession()
    {
#if ARC_TRACY
        if (!m_tracyContextCreated)
            return false;
        const bool connected = ___tracy_connected() != 0;
        const bool started = connected && !m_tracyConnected;
        m_tracyConnected = connected;
        if (started)
            ++m_tracySession;
        return started;
#else
        return false;
#endif
    }

    void GpuTimingRecorder::syncClock(int64_t gpuNowNs)
    {
#if ARC_TRACY
        if (!m_tracyContextCreated)
            return;
        ___tracy_emit_gpu_time_sync_serial({gpuNowNs, kTracyContext});
        m_tracySyncedSession = m_tracySession;
#else
        (void)gpuNowNs;
#endif
    }

    void GpuTimingRecorder::beginFrame(uint32_t slot, uint64_t frameNumber)
    {
        ARC_ASSERT(slot < kMaxFramesInFlight);
        ARC_ASSERT(!m_recording, "GPU timing frame begun twice");
        m_currentSlot = slot;
        Slot& frame = m_slots[slot];
        frame.frameNumber = frameNumber;
        frame.passCount = 0;
        frame.tracySession = m_tracyConnected && m_tracySyncedSession == m_tracySession ? m_tracySession : 0;
        m_recording = m_supported && m_enabled;
    }

    uint32_t GpuTimingRecorder::beginPass(std::string_view name)
    {
        if (!m_recording)
            return kNoPass;
        Slot& frame = m_slots[m_currentSlot];
        if (frame.passCount == kMaxPassesPerFrame)
            return kNoPass;

        const uint32_t pass = frame.passCount++;
        if (pass < frame.names.size())
            frame.names[pass].assign(name);
        else
            frame.names.emplace_back(name);

#if ARC_TRACY
        if (frame.tracySession != 0)
        {
            const std::string_view label = name.empty() ? std::string_view("Pass") : name;
            const uint64_t location = ___tracy_alloc_srcloc_name(
                __LINE__, kTracySourceFile, sizeof(kTracySourceFile) - 1, kTracyFunction, sizeof(kTracyFunction) - 1,
                label.data(), label.size(), 0);
            ___tracy_emit_gpu_zone_begin_alloc_serial({location, tracyQueryId(m_currentSlot, pass, false), kTracyContext});
        }
#endif
        return pass;
    }

    void GpuTimingRecorder::endPass(uint32_t pass)
    {
        if (pass == kNoPass)
            return;
        ARC_ASSERT(m_recording && pass < m_slots[m_currentSlot].passCount);
#if ARC_TRACY
        if (m_slots[m_currentSlot].tracySession != 0)
            ___tracy_emit_gpu_zone_end_serial({tracyQueryId(m_currentSlot, pass, true), kTracyContext});
#endif
    }

    uint32_t GpuTimingRecorder::endFrame()
    {
        const bool recording = m_recording;
        m_recording = false;
        return recording ? m_slots[m_currentSlot].passCount : 0;
    }

    void GpuTimingRecorder::resolve(uint32_t slotIndex, std::span<const int64_t> timestampsNs)
    {
        ARC_ASSERT(slotIndex < kMaxFramesInFlight);
        Slot& slot = m_slots[slotIndex];
        const uint32_t count = std::min(slot.passCount, static_cast<uint32_t>(timestampsNs.size() / 2));
        ARC_ASSERT(count == slot.passCount, "{} GPU timestamps for {} passes", timestampsNs.size(), slot.passCount);
        if (count == 0)
            return;

        int64_t frameStart = std::numeric_limits<int64_t>::max();
        int64_t frameEnd = 0;
        for (uint32_t i = 0; i < count; ++i)
        {
            const int64_t start = timestampsNs[2 * i];
            const int64_t end = timestampsNs[2 * i + 1];
            if (!validPair(start, end))
                continue;
            frameStart = std::min(frameStart, start);
            frameEnd = std::max(frameEnd, end);
        }

        m_latest.frameNumber = slot.frameNumber;
        m_latest.passes.resize(count);
        m_latest.valid = frameEnd > 0;
        m_latest.frameMs = m_latest.valid ? toMs(frameEnd - frameStart) : 0.0;
        for (uint32_t i = 0; i < count; ++i)
        {
            GpuPassTiming& pass = m_latest.passes[i];
            const int64_t start = timestampsNs[2 * i];
            const int64_t end = timestampsNs[2 * i + 1];
            pass.name = slot.names[i];
            pass.valid = validPair(start, end);
            pass.startMs = pass.valid ? toMs(start - frameStart) : 0.0;
            pass.durationMs = pass.valid ? toMs(end - start) : 0.0;
        }

        sendToTracy(slot, slotIndex, timestampsNs.first(2 * count));
        slot.passCount = 0;
    }

    uint16_t GpuTimingRecorder::tracyQueryId(uint32_t slot, uint32_t pass, bool end) const
    {
        static_assert(kMaxFramesInFlight * kTimestampsPerFrame <= std::numeric_limits<uint16_t>::max() + 1u,
                      "Tracy query ids are 16-bit");
        return static_cast<uint16_t>(slot * kTimestampsPerFrame + pass * 2 + (end ? 1 : 0));
    }

    void GpuTimingRecorder::sendToTracy(const Slot& slot, uint32_t slotIndex, std::span<const int64_t> timestampsNs) const
    {
#if ARC_TRACY
        // Zones begun in an earlier session were never seen by the profiler connected now.
        if (slot.tracySession == 0 || slot.tracySession != m_tracySession || !m_tracyConnected)
            return;

        // Every zone sent to Tracy needs both of its times, or the profiler waits for them forever, so a
        // pass without timestamps is sent as an empty zone at the end of the one before.
        int64_t previous = 0;
        for (size_t i = 0; i < timestampsNs.size() / 2; ++i)
            if (validPair(timestampsNs[2 * i], timestampsNs[2 * i + 1]))
            {
                previous = timestampsNs[2 * i];
                break;
            }

        for (uint32_t pass = 0; pass < timestampsNs.size() / 2; ++pass)
        {
            int64_t start = timestampsNs[2 * pass];
            int64_t end = timestampsNs[2 * pass + 1];
            if (!validPair(start, end))
                start = end = previous;
            previous = end;
            ___tracy_emit_gpu_time_serial({start, tracyQueryId(slotIndex, pass, false), kTracyContext});
            ___tracy_emit_gpu_time_serial({end, tracyQueryId(slotIndex, pass, true), kTracyContext});
        }
#else
        (void)slot;
        (void)slotIndex;
        (void)timestampsNs;
#endif
    }
}

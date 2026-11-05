#include "Core/Profiler.h"

#include "Core/Assert.h"
#include "Core/Log.h"
#include "Core/Time.h"

#include <cstring>

#if ARC_TRACY
    #include <tracy/TracyC.h>

    #include <cstddef>
#endif

namespace arc::profiler
{
#if ARC_TRACY
    // Zone passes a SourceLocation to Tracy as its own struct, which must therefore have the same layout.
    static_assert(sizeof(SourceLocation) == sizeof(___tracy_source_location_data));
    static_assert(offsetof(SourceLocation, name) == offsetof(___tracy_source_location_data, name));
    static_assert(offsetof(SourceLocation, function) == offsetof(___tracy_source_location_data, function));
    static_assert(offsetof(SourceLocation, file) == offsetof(___tracy_source_location_data, file));
    static_assert(offsetof(SourceLocation, line) == offsetof(___tracy_source_location_data, line));
    static_assert(offsetof(SourceLocation, color) == offsetof(___tracy_source_location_data, color));
    static_assert(sizeof(TracyCZoneCtx) <= 16);
#endif

    namespace
    {
        // Touched only by the main thread.
        struct Recorder
        {
            CpuFrame current;
            CpuFrame last;
            Clock::time_point frameStart = Clock::now();
            uint32_t depth = 0;
        };

        Recorder g_recorder;
        thread_local bool t_isMainThread = false;

        int64_t nanosecondsSince(Clock::time_point start)
        {
            return std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now() - start).count();
        }
    }

    Zone::Zone(const SourceLocation& location)
    {
        begin(location, {});
    }

    Zone::Zone(const SourceLocation& location, std::string_view name)
    {
        begin(location, name);
    }

    void Zone::begin(const SourceLocation& location, std::string_view name)
    {
#if ARC_TRACY
        // Layout-compatible, checked above.
        const TracyCZoneCtx context =
            ___tracy_emit_zone_begin(reinterpret_cast<const ___tracy_source_location_data*>(&location), 1);
        if (!name.empty())
            ___tracy_emit_zone_name(context, name.data(), name.size());
        std::memcpy(m_tracyContext, &context, sizeof(context));
#endif

        if (!t_isMainThread)
            return;
        Recorder& recorder = g_recorder;
        m_frame = recorder.current.index;
        m_index = static_cast<int32_t>(recorder.current.zones.size());
        recorder.current.zones.push_back({
            .name = name.empty() ? std::string_view(location.name) : name,
            .startNs = nanosecondsSince(recorder.frameStart),
            .endNs = -1,
            .depth = recorder.depth,
        });
        ++recorder.depth;
    }

    Zone::~Zone()
    {
#if ARC_TRACY
        TracyCZoneCtx context;
        std::memcpy(&context, m_tracyContext, sizeof(context));
        ___tracy_emit_zone_end(context);
#endif

        if (m_index < 0)
            return;
        Recorder& recorder = g_recorder;
        // A zone left open across frameMark() belongs to a frame that is gone.
        if (m_frame != recorder.current.index)
            return;
        recorder.current.zones[static_cast<size_t>(m_index)].endNs = nanosecondsSince(recorder.frameStart);
        --recorder.depth;
    }

    void setMainThread()
    {
        t_isMainThread = true;
        setThreadName("Main");
    }

    void setThreadName(const char* name)
    {
#if ARC_TRACY
        ___tracy_set_thread_name(name);
#else
        (void)name;
#endif
    }

    void frameMark()
    {
        ARC_ASSERT(t_isMainThread, "frameMark() must be called on the main thread");
        Recorder& recorder = g_recorder;
        ARC_ASSERT(recorder.depth == 0, "frameMark() called inside {} open profiling zone(s)", recorder.depth);

        recorder.current.durationNs = nanosecondsSince(recorder.frameStart);
        std::swap(recorder.last, recorder.current);
        recorder.current.zones.clear();
        recorder.current.index = recorder.last.index + 1;
        recorder.current.durationNs = 0;
        recorder.frameStart = Clock::now();
        recorder.depth = 0;

#if ARC_TRACY
        ___tracy_emit_frame_mark(nullptr);
#endif
    }

    const CpuFrame& lastCpuFrame()
    {
        return g_recorder.last;
    }

    void plot(const char* name, double value)
    {
#if ARC_TRACY
        ___tracy_emit_plot(name, value);
#else
        (void)name;
        (void)value;
#endif
    }

    void message(log::Level level, std::string_view text)
    {
#if ARC_TRACY
        const TracyMessageSeverity severity = [level] {
            switch (level)
            {
                case log::Level::Trace:   return TracyMessageSeverityTrace;
                case log::Level::Info:    return TracyMessageSeverityInfo;
                case log::Level::Warning: return TracyMessageSeverityWarning;
                case log::Level::Error:   return TracyMessageSeverityError;
                case log::Level::Fatal:   return TracyMessageSeverityFatal;
            }
            return TracyMessageSeverityInfo;
        }();
        ___tracy_emit_logString(static_cast<int8_t>(severity), 0, 0, text.size(), text.data());
#else
        (void)level;
        (void)text;
#endif
    }

    bool tracyEnabled()
    {
        return ARC_TRACY != 0;
    }

    bool tracyConnected()
    {
#if ARC_TRACY
        return ___tracy_connected() != 0;
#else
        return false;
#endif
    }
}

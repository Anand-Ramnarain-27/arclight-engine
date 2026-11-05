#pragma once

#include <cstddef>
#include <cstdint>
#include <string_view>
#include <vector>

namespace arc::log
{
    enum class Level;
}

namespace arc::profiler
{
    /// Where a zone is in the source. Must have static storage duration: Tracy keeps the pointer.
    /// Same layout as Tracy's ___tracy_source_location_data, which Profiler.cpp checks, so this
    /// header does not need Tracy's.
    struct SourceLocation
    {
        const char* name;
        const char* function;
        const char* file;
        uint32_t line;
        uint32_t color;   // 0xRRGGBB, or 0 for Tracy's default.
    };

    /// One zone recorded on the main thread. Times are nanoseconds since the frame began.
    struct CpuZone
    {
        std::string_view name;
        int64_t startNs = 0;
        int64_t endNs = 0;
        uint32_t depth = 0;   // 0 for a zone with no enclosing zone.
    };

    /// The zones of one main-thread frame, in the order they began, so a zone's children follow it.
    struct CpuFrame
    {
        uint64_t index = 0;
        int64_t durationNs = 0;
        std::vector<CpuZone> zones;
    };

    /// Times a scope: a zone in Tracy (when built with ARCLIGHT_TRACY and a profiler is connected)
    /// and, on the main thread, an entry in the frame shown by the editor's Profiler panel.
    /// Use the ARC_PROFILE_* macros rather than this class.
    class Zone
    {
    public:
        explicit Zone(const SourceLocation& location);

        /// `name` replaces the location's name. It is not copied for the editor's frame, which is read
        /// until the frame after next, so it must outlive that: a string literal or a module's name.
        Zone(const SourceLocation& location, std::string_view name);

        ~Zone();

        Zone(const Zone&) = delete;
        Zone& operator=(const Zone&) = delete;

    private:
        void begin(const SourceLocation& location, std::string_view name);

        // Tracy's zone context, copied in and out with memcpy; 16 bytes with TRACY_ON_DEMAND.
        std::byte m_tracyContext[16]{};
        uint64_t m_frame = 0;
        int32_t m_index = -1;   // Into the recorder's current frame; -1 when not recorded.
    };

    /// Makes the calling thread the one whose zones are kept for lastCpuFrame(), and names it in
    /// Tracy. Called once by the Application, before the main loop.
    void setMainThread();

    /// Names the calling thread in Tracy. `name` is copied.
    void setThreadName(const char* name);

    /// Ends the main thread's current frame and starts the next, and marks the frame boundary in
    /// Tracy. Must be called on the main thread with no zone open.
    void frameMark();

    /// The last frame completed by frameMark(). Only valid on the main thread.
    [[nodiscard]] const CpuFrame& lastCpuFrame();

    /// A value graphed over time in Tracy. `name` must have static storage duration.
    void plot(const char* name, double value);

    /// A line in Tracy's message list, at the current time. The Application sends every log message.
    void message(log::Level level, std::string_view text);

    /// True if the engine was built with Tracy (ARCLIGHT_TRACY).
    [[nodiscard]] bool tracyEnabled();

    /// True while the Tracy profiler is connected. Zones are only sent to it while it is, since the
    /// client is built on demand.
    [[nodiscard]] bool tracyConnected();
}

#define ARC_PROFILE_CONCAT_INNER(a, b) a##b
#define ARC_PROFILE_CONCAT(a, b) ARC_PROFILE_CONCAT_INNER(a, b)

/// Times the rest of the enclosing scope under `name`, a string literal.
#define ARC_PROFILE_SCOPE(name)                                                                              \
    static const ::arc::profiler::SourceLocation ARC_PROFILE_CONCAT(arcProfileLocation, __LINE__){          \
        name, __func__, __FILE__, static_cast<uint32_t>(__LINE__), 0};                                      \
    const ::arc::profiler::Zone ARC_PROFILE_CONCAT(arcProfileZone, __LINE__)(                                \
        ARC_PROFILE_CONCAT(arcProfileLocation, __LINE__))

/// Times the rest of the enclosing scope under the function's name.
#define ARC_PROFILE_FUNCTION() ARC_PROFILE_SCOPE(__func__)

/// Times the rest of the enclosing scope under a name chosen at run time. See Zone for its lifetime.
#define ARC_PROFILE_SCOPE_NAMED(label, name)                                                                 \
    static const ::arc::profiler::SourceLocation ARC_PROFILE_CONCAT(arcProfileLocation, __LINE__){          \
        label, __func__, __FILE__, static_cast<uint32_t>(__LINE__), 0};                                     \
    const ::arc::profiler::Zone ARC_PROFILE_CONCAT(arcProfileZone, __LINE__)(                                \
        ARC_PROFILE_CONCAT(arcProfileLocation, __LINE__), name)

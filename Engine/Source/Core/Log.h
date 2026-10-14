#pragma once

#include "Core/Platform.h"

#include <cstdint>
#include <format>
#include <functional>
#include <source_location>
#include <string_view>

namespace arc::log
{
    enum class Level
    {
        Trace,
        Info,
        Warning,
        Error,
        Fatal,
    };

    /// Messages below this level are discarded. Defaults to Trace in debug builds, Info otherwise.
    void setMinLevel(Level level);
    [[nodiscard]] Level minLevel();

    /// Writes a fully formatted message. Thread-safe. Prefer the ARC_LOG_* macros.
    void write(Level level, const std::source_location& where, std::string_view message);

    /// One log message as delivered to sinks. The views are valid only during the sink call.
    struct Record
    {
        Level level;
        double seconds;              // Since start-up.
        std::string_view message;
        std::string_view file;       // File name only, without the directory.
        uint32_t line;
    };

    /// Called for every message that passes the level filter, on the thread that logged it,
    /// while the log lock is held: a sink must not log, or it will deadlock.
    using Sink = std::function<void(const Record& record)>;
    using SinkId = uint32_t;

    [[nodiscard]] SinkId addSink(Sink sink);
    void removeSink(SinkId id);

    [[nodiscard]] const char* levelName(Level level);
}

// The level is checked before std::format runs, so a filtered-out message costs no formatting.
#define ARC_LOG_IMPL(level, ...)                                                        \
    do                                                                                  \
    {                                                                                   \
        if ((level) >= ::arc::log::minLevel())                                          \
            ::arc::log::write((level), std::source_location::current(),                \
                              std::format(__VA_ARGS__));                                \
    } while (0)

#if ARC_DEBUG
    #define ARC_LOG_TRACE(...) ARC_LOG_IMPL(::arc::log::Level::Trace, __VA_ARGS__)
#else
    #define ARC_LOG_TRACE(...) do { } while (0)
#endif

#define ARC_LOG_INFO(...)  ARC_LOG_IMPL(::arc::log::Level::Info,    __VA_ARGS__)
#define ARC_LOG_WARN(...)  ARC_LOG_IMPL(::arc::log::Level::Warning, __VA_ARGS__)
#define ARC_LOG_ERROR(...) ARC_LOG_IMPL(::arc::log::Level::Error,   __VA_ARGS__)
#define ARC_LOG_FATAL(...) ARC_LOG_IMPL(::arc::log::Level::Fatal,   __VA_ARGS__)

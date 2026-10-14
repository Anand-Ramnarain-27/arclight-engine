#include "Core/Log.h"

#include <atomic>
#include <chrono>
#include <cstdio>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

#if ARC_PLATFORM_WINDOWS
    // Included only in this .cpp so <windows.h> never leaks into engine headers.
    #define WIN32_LEAN_AND_MEAN
    #define NOMINMAX
    #include <windows.h>
#endif

namespace arc::log
{
    namespace
    {
        std::atomic<Level> g_minLevel{ARC_DEBUG ? Level::Trace : Level::Info};

        std::mutex g_mutex;
        std::vector<std::pair<SinkId, Sink>> g_sinks;
        SinkId g_nextSinkId = 1;

        const auto g_startTime = std::chrono::steady_clock::now();

        constexpr std::string_view fileName(std::string_view path)
        {
            const auto slash = path.find_last_of("/\\");
            return slash == std::string_view::npos ? path : path.substr(slash + 1);
        }
    }

    const char* levelName(Level level)
    {
        switch (level)
        {
            case Level::Trace:   return "TRACE";
            case Level::Info:    return "INFO";
            case Level::Warning: return "WARN";
            case Level::Error:   return "ERROR";
            case Level::Fatal:   return "FATAL";
        }
        return "?";
    }

    void setMinLevel(Level level)
    {
        g_minLevel.store(level, std::memory_order_relaxed);
    }

    Level minLevel()
    {
        return g_minLevel.load(std::memory_order_relaxed);
    }

    SinkId addSink(Sink sink)
    {
        const std::scoped_lock lock(g_mutex);
        const SinkId id = g_nextSinkId++;
        g_sinks.emplace_back(id, std::move(sink));
        return id;
    }

    void removeSink(SinkId id)
    {
        const std::scoped_lock lock(g_mutex);
        std::erase_if(g_sinks, [id](const auto& entry) { return entry.first == id; });
    }

    void write(Level level, const std::source_location& where, std::string_view message)
    {
        const double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - g_startTime).count();
        const std::string_view file = fileName(where.file_name());

        // Formatted before taking the lock and written in a single call, so lines
        // from different threads never interleave.
        const std::string line = std::format("[{:9.3f}] {:<5} {} ({}:{})\n", seconds, levelName(level), message, file, where.line());

        const std::scoped_lock lock(g_mutex);

        FILE* stream = level >= Level::Warning ? stderr : stdout;
        std::fwrite(line.data(), 1, line.size(), stream);
        std::fflush(stream);

#if ARC_PLATFORM_WINDOWS
        // The Visual Studio Output window only shows OutputDebugString, not stdout.
        OutputDebugStringA(line.c_str());
#endif

        const Record record{level, seconds, message, file, where.line()};
        for (const auto& [id, sink] : g_sinks)
            sink(record);
    }
}

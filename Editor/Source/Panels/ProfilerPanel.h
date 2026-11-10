#pragma once

#include "Panels/Panel.h"

#include <array>
#include <cstdint>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace arc
{
    class Application;

    /// Where the main thread's frame time goes (the ARC_PROFILE_* zones), how long each render pass
    /// takes on the GPU, and whether Tracy is connected. Times are averaged over a refresh interval so
    /// they can be read; the GPU timeline shows the newest frame as it is.
    class ProfilerPanel final : public Panel
    {
    public:
        explicit ProfilerPanel(Application& app) : m_app(app) {}

        [[nodiscard]] const char* title() const override { return "Profiler"; }
        void draw(bool* open) override;

    private:
        /// Per-frame times of named entries, averaged over the frames since the last publish(). An
        /// entry is identified by its path, so zones of the same name under different parents stay apart.
        class TimingTable
        {
        public:
            struct Row
            {
                std::string name;
                uint32_t depth = 0;
                double averageMs = 0.0;   // Per frame, all calls together.
                double maxMs = 0.0;       // The most in one frame.
                double callsPerFrame = 0.0;
            };

            void beginFrame();
            void add(const std::string& path, std::string_view name, uint32_t depth, double ms);
            void endFrame();

            /// Replaces the rows with the averages since the last call, in the order of the newest frame.
            void publish();

            [[nodiscard]] const std::vector<Row>& rows() const { return m_rows; }

        private:
            struct Entry
            {
                std::string name;
                uint32_t depth = 0;
                double frameMs = 0.0;
                uint32_t frameCalls = 0;
                double totalMs = 0.0;
                double maxMs = 0.0;
                uint64_t totalCalls = 0;
            };

            std::unordered_map<std::string, Entry> m_entries;
            std::vector<std::string> m_frameOrder;   // Paths in the order they first appear this frame.
            std::vector<std::string> m_lastOrder;
            uint32_t m_frames = 0;
            std::vector<Row> m_rows;
        };

        void collect();
        void drawStatus();
        void drawHistory();
        void drawCpuTable();
        void drawGpuSection();

        static constexpr size_t kHistorySize = 240;

        Application& m_app;
        TimingTable m_cpu;
        TimingTable m_gpu;
        double m_cpuFrameMs = 0.0;   // Averages over the last interval.
        double m_gpuFrameMs = 0.0;
        double m_cpuFrameSum = 0.0;
        double m_gpuFrameSum = 0.0;
        uint32_t m_cpuFrames = 0;
        uint32_t m_gpuFrames = 0;
        double m_sinceRefresh = 0.0;
        float m_refreshSeconds = 0.5f;
        bool m_paused = false;
        uint64_t m_lastCpuFrame = ~0ull;
        uint64_t m_lastGpuFrame = ~0ull;
        uint64_t m_jobsRunBefore = 0;
        uint64_t m_jobsLastFrame = 0;

        std::array<float, kHistorySize> m_cpuHistory{};
        std::array<float, kHistorySize> m_gpuHistory{};
        size_t m_historyNext = 0;
        std::vector<std::string> m_pathStack;
    };
}

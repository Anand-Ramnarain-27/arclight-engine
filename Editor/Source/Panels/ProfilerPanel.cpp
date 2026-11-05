#include "Panels/ProfilerPanel.h"

#include "Application/Application.h"
#include "Core/Profiler.h"
#include "RHI/Device.h"

#include <imgui.h>

#include <algorithm>
#include <cmath>
#include <format>

namespace arc
{
    namespace
    {
        // Tracy's network protocol changes between versions, so the profiler must be this release.
        constexpr const char* kTracyVersion = "0.14.1";

        ImU32 passColor(size_t index, float alpha)
        {
            // Golden-ratio hue steps keep neighbouring passes apart.
            const float hue = std::fmod(static_cast<float>(index) * 0.618034f, 1.0f);
            float r = 0.0f;
            float g = 0.0f;
            float b = 0.0f;
            ImGui::ColorConvertHSVtoRGB(hue, 0.55f, 0.85f, r, g, b);
            return ImGui::GetColorU32(ImVec4(r, g, b, alpha));
        }

        void timeCell(double ms)
        {
            ImGui::Text("%.3f", ms);
        }
    }

    void ProfilerPanel::TimingTable::beginFrame()
    {
        m_frameOrder.clear();
        for (auto& [path, entry] : m_entries)
        {
            entry.frameMs = 0.0;
            entry.frameCalls = 0;
        }
    }

    void ProfilerPanel::TimingTable::add(const std::string& path, std::string_view name, uint32_t depth, double ms)
    {
        auto [it, inserted] = m_entries.try_emplace(path);
        Entry& entry = it->second;
        if (inserted)
        {
            entry.name = name;
            entry.depth = depth;
        }
        if (entry.frameCalls == 0)
            m_frameOrder.push_back(path);
        entry.frameMs += ms;
        ++entry.frameCalls;
    }

    void ProfilerPanel::TimingTable::endFrame()
    {
        for (const std::string& path : m_frameOrder)
        {
            Entry& entry = m_entries[path];
            entry.totalMs += entry.frameMs;
            entry.maxMs = std::max(entry.maxMs, entry.frameMs);
            entry.totalCalls += entry.frameCalls;
        }
        m_lastOrder = m_frameOrder;
        ++m_frames;
    }

    void ProfilerPanel::TimingTable::publish()
    {
        if (m_frames == 0)
            return;

        m_rows.clear();
        for (const std::string& path : m_lastOrder)
        {
            const Entry& entry = m_entries[path];
            m_rows.push_back({
                .name = entry.name,
                .depth = entry.depth,
                .averageMs = entry.totalMs / m_frames,
                .maxMs = entry.maxMs,
                .callsPerFrame = static_cast<double>(entry.totalCalls) / m_frames,
            });
        }

        // Entries that did not appear in the newest frame are dropped, so the map does not grow with
        // every zone name ever seen (per-module zones of removed modules, renamed passes).
        std::erase_if(m_entries, [this](const auto& item) {
            return std::find(m_lastOrder.begin(), m_lastOrder.end(), item.first) == m_lastOrder.end();
        });
        for (auto& [path, entry] : m_entries)
        {
            entry.totalMs = 0.0;
            entry.maxMs = 0.0;
            entry.totalCalls = 0;
        }
        m_frames = 0;
    }

    void ProfilerPanel::collect()
    {
        const profiler::CpuFrame& cpu = profiler::lastCpuFrame();
        if (cpu.index == m_lastCpuFrame || cpu.durationNs == 0)
            return;
        m_lastCpuFrame = cpu.index;

        m_cpu.beginFrame();
        m_pathStack.clear();
        for (const profiler::CpuZone& zone : cpu.zones)
        {
            // A zone still open when the frame ended has no end time.
            if (zone.endNs < zone.startNs)
                continue;
            m_pathStack.resize(zone.depth);
            std::string path = zone.depth > 0 && !m_pathStack.empty() ? m_pathStack.back() + "/" : std::string();
            path += zone.name;
            m_pathStack.push_back(path);
            m_cpu.add(path, zone.name, zone.depth, static_cast<double>(zone.endNs - zone.startNs) * 1e-6);
        }
        m_cpu.endFrame();
        const double cpuMs = static_cast<double>(cpu.durationNs) * 1e-6;
        m_cpuFrameSum += cpuMs;
        ++m_cpuFrames;

        const rhi::GpuFrameTimings& gpu = m_app.renderer().device().gpuTimings();
        if (gpu.valid && gpu.frameNumber != m_lastGpuFrame)
        {
            m_lastGpuFrame = gpu.frameNumber;
            m_gpu.beginFrame();
            for (const rhi::GpuPassTiming& pass : gpu.passes)
                if (pass.valid)
                    m_gpu.add(pass.name, pass.name, 0, pass.durationMs);
            m_gpu.endFrame();
            m_gpuFrameSum += gpu.frameMs;
            ++m_gpuFrames;
        }

        m_cpuHistory[m_historyNext] = static_cast<float>(cpuMs);
        m_gpuHistory[m_historyNext] = gpu.valid ? static_cast<float>(gpu.frameMs) : 0.0f;
        m_historyNext = (m_historyNext + 1) % kHistorySize;

        m_sinceRefresh += cpuMs * 1e-3;
        if (m_sinceRefresh >= m_refreshSeconds)
        {
            m_cpu.publish();
            m_gpu.publish();
            m_cpuFrameMs = m_cpuFrames > 0 ? m_cpuFrameSum / m_cpuFrames : 0.0;
            m_gpuFrameMs = m_gpuFrames > 0 ? m_gpuFrameSum / m_gpuFrames : 0.0;
            m_cpuFrameSum = 0.0;
            m_gpuFrameSum = 0.0;
            m_cpuFrames = 0;
            m_gpuFrames = 0;
            m_sinceRefresh = 0.0;
        }
    }

    void ProfilerPanel::draw(bool* open)
    {
        if (!ImGui::Begin(title(), open))
        {
            ImGui::End();
            return;
        }

        if (!m_paused)
            collect();

        drawStatus();
        drawHistory();
        if (ImGui::BeginTabBar("##profiler"))
        {
            if (ImGui::BeginTabItem("CPU (main thread)"))
            {
                drawCpuTable();
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem("GPU passes"))
            {
                drawGpuSection();
                ImGui::EndTabItem();
            }
            ImGui::EndTabBar();
        }

        ImGui::End();
    }

    void ProfilerPanel::drawStatus()
    {
        ImGui::Checkbox("Pause", &m_paused);
        ImGui::SameLine();
        ImGui::SetNextItemWidth(ImGui::GetFontSize() * 8.0f);
        ImGui::SliderFloat("Average over", &m_refreshSeconds, 0.1f, 2.0f, "%.1f s");

        rhi::Device& device = m_app.renderer().device();
        if (device.gpuTimingSupported())
        {
            ImGui::SameLine();
            bool gpuTiming = device.gpuTimingEnabled();
            if (ImGui::Checkbox("GPU timing", &gpuTiming))
                device.setGpuTimingEnabled(gpuTiming);
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Two GPU timestamps per render pass, read back %u frames later.", rhi::kMaxFramesInFlight);
        }

        if (!profiler::tracyEnabled())
            ImGui::TextDisabled("Tracy: not built in (ARCLIGHT_TRACY is OFF)");
        else if (profiler::tracyConnected())
            ImGui::TextColored(ImVec4(0.45f, 0.85f, 0.45f, 1.0f), "Tracy: connected");
        else
            ImGui::TextDisabled("Tracy: waiting. Open Tracy %s and connect to 127.0.0.1 for every thread and frame.", kTracyVersion);
    }

    void ProfilerPanel::drawHistory()
    {
        float maxMs = 16.7f;
        for (size_t i = 0; i < kHistorySize; ++i)
            maxMs = std::max({maxMs, m_cpuHistory[i], m_gpuHistory[i]});
        maxMs *= 1.1f;

        const ImVec2 size(-1.0f, ImGui::GetFontSize() * 3.5f);
        const int offset = static_cast<int>(m_historyNext);
        const std::string cpuOverlay = std::format("CPU frame {:.2f} ms", m_cpuFrameMs);
        ImGui::PlotLines("##cpu", m_cpuHistory.data(), static_cast<int>(kHistorySize), offset, cpuOverlay.c_str(), 0.0f,
                         maxMs, size);
        if (m_app.renderer().device().gpuTimingSupported())
        {
            const std::string gpuOverlay = std::format("GPU frame {:.2f} ms", m_gpuFrameMs);
            ImGui::PlotLines("##gpu", m_gpuHistory.data(), static_cast<int>(kHistorySize), offset, gpuOverlay.c_str(),
                             0.0f, maxMs, size);
        }
    }

    void ProfilerPanel::drawCpuTable()
    {
        const auto& rows = m_cpu.rows();
        if (rows.empty())
        {
            ImGui::TextDisabled("Collecting...");
            return;
        }

        constexpr ImGuiTableFlags flags = ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_Resizable |
                                          ImGuiTableFlags_ScrollY | ImGuiTableFlags_SizingFixedFit;
        if (!ImGui::BeginTable("##cpu", 5, flags))
            return;
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableSetupColumn("Zone", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("ms");
        ImGui::TableSetupColumn("max ms");
        ImGui::TableSetupColumn("calls");
        ImGui::TableSetupColumn("of frame", ImGuiTableColumnFlags_WidthFixed, ImGui::GetFontSize() * 7.0f);
        ImGui::TableHeadersRow();

        // Rows are in pre-order with depths: a collapsed node skips every following row deeper than it,
        // and an open one is popped when a row at its depth or above comes.
        std::vector<uint32_t> openDepths;
        size_t i = 0;
        while (i < rows.size())
        {
            const TimingTable::Row& row = rows[i];
            while (!openDepths.empty() && openDepths.back() >= row.depth)
            {
                ImGui::TreePop();
                openDepths.pop_back();
            }
            const bool hasChildren = i + 1 < rows.size() && rows[i + 1].depth > row.depth;

            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGuiTreeNodeFlags nodeFlags = ImGuiTreeNodeFlags_SpanAllColumns | ImGuiTreeNodeFlags_DefaultOpen;
            if (!hasChildren)
                nodeFlags |= ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen;
            ImGui::PushID(static_cast<int>(i));
            const bool opened = ImGui::TreeNodeEx(row.name.c_str(), nodeFlags);
            ImGui::PopID();

            ImGui::TableSetColumnIndex(1);
            timeCell(row.averageMs);
            ImGui::TableSetColumnIndex(2);
            timeCell(row.maxMs);
            ImGui::TableSetColumnIndex(3);
            ImGui::Text("%.0f", row.callsPerFrame);
            ImGui::TableSetColumnIndex(4);
            const float fraction = m_cpuFrameMs > 0.0 ? static_cast<float>(row.averageMs / m_cpuFrameMs) : 0.0f;
            const std::string percent = std::format("{:.0f}%", fraction * 100.0f);
            ImGui::ProgressBar(std::clamp(fraction, 0.0f, 1.0f), ImVec2(-1.0f, 0.0f), percent.c_str());

            ++i;
            if (hasChildren && opened)
                openDepths.push_back(row.depth);
            else if (hasChildren)
                while (i < rows.size() && rows[i].depth > row.depth)
                    ++i;
        }
        for (size_t level = 0; level < openDepths.size(); ++level)
            ImGui::TreePop();
        ImGui::EndTable();
    }

    void ProfilerPanel::drawGpuSection()
    {
        const rhi::Device& device = m_app.renderer().device();
        if (!device.gpuTimingSupported())
        {
            ImGui::TextDisabled("This GPU or backend has no timestamps at render pass boundaries.");
            return;
        }
        if (!device.gpuTimingEnabled())
        {
            ImGui::TextDisabled("GPU timing is off.");
            return;
        }

        // The newest frame as it ran: on tile-based GPUs (Apple), passes overlap.
        const rhi::GpuFrameTimings& frame = device.gpuTimings();
        if (frame.valid && frame.frameMs > 0.0)
        {
            ImGui::TextDisabled("Frame %llu, %.3f ms from the first pass's start to the last pass's end",
                                static_cast<unsigned long long>(frame.frameNumber), frame.frameMs);
            const float rowHeight = ImGui::GetFontSize();
            const float width = ImGui::GetContentRegionAvail().x;
            const ImVec2 origin = ImGui::GetCursorScreenPos();
            const auto passCount = static_cast<float>(frame.passes.size());
            ImGui::InvisibleButton("##timeline", ImVec2(width, rowHeight * std::max(passCount, 1.0f)));
            const bool hovered = ImGui::IsItemHovered();
            const ImVec2 mouse = ImGui::GetIO().MousePos;
            ImDrawList* drawList = ImGui::GetWindowDrawList();
            const auto scale = static_cast<float>(width / frame.frameMs);
            for (size_t i = 0; i < frame.passes.size(); ++i)
            {
                const rhi::GpuPassTiming& pass = frame.passes[i];
                if (!pass.valid)
                    continue;
                const ImVec2 min(origin.x + static_cast<float>(pass.startMs) * scale, origin.y + rowHeight * static_cast<float>(i));
                // At least a pixel wide, so very short passes stay visible.
                const ImVec2 max(std::max(min.x + 1.0f, min.x + static_cast<float>(pass.durationMs) * scale), min.y + rowHeight - 1.0f);
                drawList->AddRectFilled(min, max, passColor(i, 0.9f));
                if (hovered && mouse.y >= min.y && mouse.y < max.y)
                    ImGui::SetTooltip("%s\nstarts %.3f ms, takes %.3f ms", pass.name.c_str(), pass.startMs, pass.durationMs);
            }
        }

        const auto& rows = m_gpu.rows();
        constexpr ImGuiTableFlags flags = ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_Resizable |
                                          ImGuiTableFlags_ScrollY | ImGuiTableFlags_SizingFixedFit;
        if (rows.empty() || !ImGui::BeginTable("##gpu", 4, flags))
            return;
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableSetupColumn("Render pass", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("ms");
        ImGui::TableSetupColumn("max ms");
        ImGui::TableSetupColumn("of frame", ImGuiTableColumnFlags_WidthFixed, ImGui::GetFontSize() * 7.0f);
        ImGui::TableHeadersRow();
        for (const TimingTable::Row& row : rows)
        {
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            if (row.callsPerFrame > 1.0)
                ImGui::Text("%s (x%.0f)", row.name.c_str(), row.callsPerFrame);
            else
                ImGui::TextUnformatted(row.name.c_str());
            ImGui::TableSetColumnIndex(1);
            timeCell(row.averageMs);
            ImGui::TableSetColumnIndex(2);
            timeCell(row.maxMs);
            ImGui::TableSetColumnIndex(3);
            const float fraction = m_gpuFrameMs > 0.0 ? static_cast<float>(row.averageMs / m_gpuFrameMs) : 0.0f;
            const std::string percent = std::format("{:.0f}%", fraction * 100.0f);
            ImGui::ProgressBar(std::clamp(fraction, 0.0f, 1.0f), ImVec2(-1.0f, 0.0f), percent.c_str());
        }
        ImGui::EndTable();
    }
}

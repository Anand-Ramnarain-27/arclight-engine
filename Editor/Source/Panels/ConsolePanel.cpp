#include "Panels/ConsolePanel.h"

#include <format>
#include <vector>

namespace arc
{
    namespace
    {
        ImVec4 levelColor(log::Level level)
        {
            switch (level)
            {
                case log::Level::Trace:   return {0.55f, 0.58f, 0.64f, 1.0f};
                case log::Level::Info:    return {0.88f, 0.90f, 0.94f, 1.0f};
                case log::Level::Warning: return {0.98f, 0.78f, 0.30f, 1.0f};
                case log::Level::Error:   return {0.97f, 0.42f, 0.40f, 1.0f};
                case log::Level::Fatal:   return {1.00f, 0.30f, 0.62f, 1.0f};
            }
            return {1.0f, 1.0f, 1.0f, 1.0f};
        }
    }

    ConsolePanel::ConsolePanel()
    {
        m_sinkId = log::addSink([this](const log::Record& record) { addEntry(record); });
    }

    ConsolePanel::~ConsolePanel()
    {
        log::removeSink(m_sinkId);
    }

    void ConsolePanel::addEntry(const log::Record& record)
    {
        std::string text = std::format("[{:8.3f}] {:<5} {}  ({}:{})", record.seconds, log::levelName(record.level),
                                       record.message, record.file, record.line);

        const std::scoped_lock lock(m_mutex);
        if (m_entries.size() == kMaxEntries)
            m_entries.pop_front();
        m_entries.push_back({record.level, std::move(text)});
    }

    bool ConsolePanel::isVisible(const Entry& entry) const
    {
        return m_showLevel[static_cast<size_t>(entry.level)] && m_filter.PassFilter(entry.text.c_str());
    }

    void ConsolePanel::drawToolbar()
    {
        if (ImGui::Button("Clear"))
        {
            const std::scoped_lock lock(m_mutex);
            m_entries.clear();
        }
        ImGui::SameLine();

        if (ImGui::Button("Copy"))
        {
            std::string all;
            {
                const std::scoped_lock lock(m_mutex);
                for (const Entry& entry : m_entries)
                    if (isVisible(entry))
                        all.append(entry.text).push_back('\n');
            }
            ImGui::SetClipboardText(all.c_str());
        }
        ImGui::SameLine();
        ImGui::Checkbox("Auto-scroll", &m_autoScroll);

        for (size_t i = 0; i < kLevelCount; ++i)
        {
            ImGui::SameLine();
            const auto level = static_cast<log::Level>(i);
            ImGui::PushStyleColor(ImGuiCol_Text, levelColor(level));
            ImGui::Checkbox(log::levelName(level), &m_showLevel[i]);
            ImGui::PopStyleColor();
        }

        m_filter.Draw("Filter (inc,-exc)", ImGui::GetFontSize() * 16.0f);
    }

    void ConsolePanel::draw(bool* open)
    {
        if (!ImGui::Begin(title(), open))
        {
            ImGui::End();
            return;
        }

        drawToolbar();
        ImGui::Separator();

        if (ImGui::BeginChild("##log", ImVec2(0, 0), ImGuiChildFlags_None, ImGuiWindowFlags_HorizontalScrollbar))
        {
            const std::scoped_lock lock(m_mutex);

            std::vector<const Entry*> visible;
            visible.reserve(m_entries.size());
            for (const Entry& entry : m_entries)
                if (isVisible(entry))
                    visible.push_back(&entry);

            // Only the rows on screen are submitted, so thousands of entries stay cheap.
            ImGuiListClipper clipper;
            clipper.Begin(static_cast<int>(visible.size()));
            while (clipper.Step())
            {
                for (int row = clipper.DisplayStart; row < clipper.DisplayEnd; ++row)
                {
                    const Entry& entry = *visible[static_cast<size_t>(row)];
                    ImGui::PushStyleColor(ImGuiCol_Text, levelColor(entry.level));
                    ImGui::TextUnformatted(entry.text.c_str(), entry.text.c_str() + entry.text.size());
                    ImGui::PopStyleColor();
                }
            }

            // Follow new output only while the view is already at the bottom, so scrolling up to read stays put.
            if (m_autoScroll && ImGui::GetScrollY() >= ImGui::GetScrollMaxY())
                ImGui::SetScrollHereY(1.0f);
        }
        ImGui::EndChild();
        ImGui::End();
    }
}

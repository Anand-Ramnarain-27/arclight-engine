#pragma once

#include "Core/Log.h"
#include "Panels/Panel.h"

#include <imgui.h>

#include <array>
#include <cstddef>
#include <deque>
#include <mutex>
#include <string>

namespace arc
{
    /// Shows engine log output. Receives messages through a log sink, from any thread.
    class ConsolePanel final : public Panel
    {
    public:
        ConsolePanel();
        ~ConsolePanel() override;

        ConsolePanel(const ConsolePanel&) = delete;
        ConsolePanel& operator=(const ConsolePanel&) = delete;

        [[nodiscard]] const char* title() const override { return "Console"; }
        void draw(bool* open) override;

    private:
        struct Entry
        {
            log::Level level;
            std::string text;
        };

        static constexpr size_t kMaxEntries = 2000;
        static constexpr size_t kLevelCount = 5;

        void addEntry(const log::Record& record);
        void drawToolbar();
        [[nodiscard]] bool isVisible(const Entry& entry) const;

        // Guards m_entries. Never log while holding it: the sink runs under the log lock,
        // so logging here would lock in the opposite order and could deadlock.
        std::mutex m_mutex;
        std::deque<Entry> m_entries;

        log::SinkId m_sinkId = 0;
        ImGuiTextFilter m_filter;
        std::array<bool, kLevelCount> m_showLevel{true, true, true, true, true};
        bool m_autoScroll = true;
    };
}

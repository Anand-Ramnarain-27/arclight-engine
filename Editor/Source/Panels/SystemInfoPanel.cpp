#include "Panels/SystemInfoPanel.h"

#include "Application/Application.h"
#include "RHI/Device.h"

#include <imgui.h>

#include <format>
#include <string>

namespace arc
{
    namespace
    {
        void row(const char* label, const std::string& value)
        {
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::TextDisabled("%s", label);
            ImGui::TableSetColumnIndex(1);
            ImGui::TextUnformatted(value.c_str());
        }

        void section(const char* name)
        {
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::SeparatorText(name);
        }
    }

    void SystemInfoPanel::draw(bool* open)
    {
        if (!ImGui::Begin(title(), open))
        {
            ImGui::End();
            return;
        }

        if (!m_info || ImGui::Button("Refresh"))
            m_info = querySystemInfo(m_app.window());

        const SystemInfo& info = *m_info;

        std::string features;
        for (const std::string& feature : info.cpuFeatures)
            features += (features.empty() ? "" : ", ") + feature;

        if (ImGui::BeginTable("##system", 2, ImGuiTableFlags_SizingStretchProp))
        {
            section("Build");
            row("Arclight", ARCLIGHT_VERSION);
            row("Configuration", info.buildConfiguration);
            row("Compiler", info.compiler);
            row("SDL", info.sdlVersion);
            row("Dear ImGui", IMGUI_VERSION);

            section("System");
            row("Platform", info.platform);
            row("CPU cores", std::format("{} logical", info.logicalCpuCores));
            row("CPU features", features.empty() ? "none detected" : features);
            row("Cache line", std::format("{} bytes", info.cpuCacheLineBytes));
            row("RAM", std::format("{:.1f} GB", info.systemRamMB / 1024.0));

            section("Display");
            row("Monitor", info.display.name);
            row("Resolution", std::format("{} x {} px @ {:.0f} Hz", info.display.width, info.display.height,
                                          info.display.refreshRate));
            row("UI scale", std::format("{:.0f}%", info.display.contentScale * 100.0f));
            row("Video driver", info.videoDriver);

            const rhi::AdapterInfo& adapter = m_app.renderer().device().adapterInfo();
            section("Graphics");
            row("API", rhi::backendName(adapter.backend));
            row("GPU", adapter.name);
            row(adapter.unifiedMemory ? "Memory (unified)" : "Video memory", std::format("{} MB", adapter.videoMemoryMB));
            if (!adapter.driver.empty())
                row("Driver", adapter.driver);
            row("Frames in flight", std::format("{}", rhi::kMaxFramesInFlight));

            ImGui::EndTable();
        }

        ImGui::End();
    }
}

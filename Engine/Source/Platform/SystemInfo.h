#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace arc
{
    class Window;

    struct DisplayInfo
    {
        std::string name;
        uint32_t width = 0;          // Pixels.
        uint32_t height = 0;         // Pixels.
        float refreshRate = 0.0f;    // Hz; 0 if unknown.
        float contentScale = 1.0f;   // OS UI scale, e.g. 1.5 for 150%.
    };

    struct SystemInfo
    {
        std::string platform;
        std::string buildConfiguration;
        std::string compiler;
        std::string sdlVersion;
        std::string videoDriver;

        int logicalCpuCores = 0;
        int cpuCacheLineBytes = 0;
        int systemRamMB = 0;
        std::vector<std::string> cpuFeatures;

        DisplayInfo display;
    };

    /// Snapshot of the machine and display the window is on. SDL video must be initialised.
    [[nodiscard]] SystemInfo querySystemInfo(const Window& window);
}

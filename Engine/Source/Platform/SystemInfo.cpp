#include "Platform/SystemInfo.h"

#include "Core/Platform.h"
#include "Platform/Window.h"

#include <SDL3/SDL.h>

#include <format>

namespace arc
{
    namespace
    {
        std::string compilerDescription()
        {
#if ARC_COMPILER_CLANG
            return std::format("Clang {}.{}.{}", __clang_major__, __clang_minor__, __clang_patchlevel__);
#elif ARC_COMPILER_MSVC
            return std::format("MSVC {}", _MSC_FULL_VER);
#else
            return std::format("GCC {}.{}.{}", __GNUC__, __GNUC_MINOR__, __GNUC_PATCHLEVEL__);
#endif
        }

        std::vector<std::string> cpuFeatures()
        {
            std::vector<std::string> features;
            const auto add = [&](bool present, const char* name) {
                if (present)
                    features.emplace_back(name);
            };
            add(SDL_HasSSE2(), "SSE2");
            add(SDL_HasSSE41(), "SSE4.1");
            add(SDL_HasSSE42(), "SSE4.2");
            add(SDL_HasAVX(), "AVX");
            add(SDL_HasAVX2(), "AVX2");
            add(SDL_HasAVX512F(), "AVX-512F");
            add(SDL_HasNEON(), "NEON");
            return features;
        }
    }

    SystemInfo querySystemInfo(const Window& window)
    {
        SystemInfo info;
        info.platform = SDL_GetPlatform();
        info.buildConfiguration = ARC_DEBUG ? "Debug" : "Release";
        info.compiler = compilerDescription();

        const int version = SDL_GetVersion();
        info.sdlVersion = std::format("{}.{}.{}", SDL_VERSIONNUM_MAJOR(version), SDL_VERSIONNUM_MINOR(version),
                                      SDL_VERSIONNUM_MICRO(version));

        const char* driver = SDL_GetCurrentVideoDriver();
        info.videoDriver = driver ? driver : "none";

        info.logicalCpuCores = SDL_GetNumLogicalCPUCores();
        info.cpuCacheLineBytes = SDL_GetCPUCacheLineSize();
        info.systemRamMB = SDL_GetSystemRAM();
        info.cpuFeatures = cpuFeatures();

        const SDL_DisplayID display = SDL_GetDisplayForWindow(window.sdlWindow());
        if (display != 0)
        {
            const char* name = SDL_GetDisplayName(display);
            info.display.name = name ? name : "Unknown display";
            info.display.contentScale = SDL_GetDisplayContentScale(display);
            if (const SDL_DisplayMode* mode = SDL_GetCurrentDisplayMode(display))
            {
                // SDL display modes are in points; pixel_density converts to pixels.
                info.display.width = static_cast<uint32_t>(static_cast<float>(mode->w) * mode->pixel_density);
                info.display.height = static_cast<uint32_t>(static_cast<float>(mode->h) * mode->pixel_density);
                info.display.refreshRate = mode->refresh_rate;
            }
        }

        return info;
    }
}

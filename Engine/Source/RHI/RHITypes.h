#pragma once

#include "Core/Platform.h"
#include "Platform/Window.h"

#include <cstdint>
#include <string>

namespace arc::rhi
{
    /// How many frames the CPU may record ahead of the GPU. Per-frame resources (command
    /// allocators, upload memory) are created this many times and reused round-robin.
    inline constexpr uint32_t kMaxFramesInFlight = 2;

    enum class Backend
    {
        D3D12,
        Metal,
        Null,
    };

    [[nodiscard]] const char* backendName(Backend backend);

    enum class Format
    {
        Unknown,
        RGBA8_UNORM,
        BGRA8_UNORM,
    };

    enum class LoadOp
    {
        Load,
        Clear,
        DontCare,
    };

    enum class StoreOp
    {
        Store,
        DontCare,
    };

    struct ClearColor
    {
        float r = 0.0f;
        float g = 0.0f;
        float b = 0.0f;
        float a = 1.0f;
    };

    struct DeviceDesc
    {
        /// D3D12 debug layer. On Metal, API validation is enabled by the Xcode scheme or by
        /// launching with MTL_DEBUG_LAYER=1, so this flag has no effect there.
        bool debugLayer = ARC_DEBUG;

        /// D3D12 GPU-based validation. Catches errors the debug layer cannot see (descriptor
        /// contents, resource states inside shaders), at a large GPU cost.
        bool gpuValidation = false;
    };

    struct AdapterInfo
    {
        Backend backend = Backend::Null;
        std::string name;
        std::string driver;
        uint64_t videoMemoryMB = 0;
        bool unifiedMemory = false;
    };

    struct SwapchainDesc
    {
        Extent2D size;               // Pixels.
        Format format = Format::BGRA8_UNORM;
        bool vsync = true;
    };

    struct TextureDesc
    {
        Extent2D size;               // Pixels.
        Format format = Format::Unknown;
    };
}

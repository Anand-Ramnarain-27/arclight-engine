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

    /// Where a buffer lives, which decides who can read and write it.
    enum class MemoryType
    {
        /// GPU memory the CPU cannot access. Fastest for the GPU; filled by copying from an
        /// Upload buffer. D3D12 default heap, Metal private storage.
        GpuOnly,
        /// CPU-writable, GPU-readable, mapped for the buffer's lifetime. Write-combined: the CPU
        /// should only write it sequentially, never read it. D3D12 upload heap, Metal shared storage.
        Upload,
        /// GPU-writable, CPU-readable, mapped for the buffer's lifetime. The CPU may read it once
        /// the GPU work that wrote it has completed. D3D12 readback heap, Metal shared storage.
        Readback,
    };

    enum class BufferUsage : uint32_t
    {
        None     = 0,
        Vertex   = 1 << 0,
        Index    = 1 << 1,
        Constant = 1 << 2,
    };

    [[nodiscard]] constexpr BufferUsage operator|(BufferUsage a, BufferUsage b)
    {
        return static_cast<BufferUsage>(static_cast<uint32_t>(a) | static_cast<uint32_t>(b));
    }

    [[nodiscard]] constexpr bool hasFlag(BufferUsage flags, BufferUsage flag)
    {
        return (static_cast<uint32_t>(flags) & static_cast<uint32_t>(flag)) != 0;
    }

    /// How the GPU is about to use a resource. A barrier moves a resource between states; on
    /// D3D12 this is a transition barrier, on Metal (which tracks hazards itself) it only
    /// updates the tracked state.
    enum class ResourceState
    {
        Common,
        CopySource,
        CopyDest,
        VertexBuffer,
        IndexBuffer,
        ConstantBuffer,
        ShaderRead,
        RenderTarget,
        Present,
    };

    [[nodiscard]] const char* resourceStateName(ResourceState state);

    struct BufferDesc
    {
        uint64_t size = 0;           // Bytes.
        BufferUsage usage = BufferUsage::None;
        MemoryType memory = MemoryType::GpuOnly;
        std::string debugName{};
    };

    enum class ShaderStage
    {
        Vertex,
        Pixel,
    };

    /// The compiled shader format a backend consumes; decides which file the loader reads.
    enum class ShaderFormat
    {
        DXIL,       // D3D12. Also accepted by the Null backend.
        MetalLib,   // Metal.
    };

    [[nodiscard]] const char* shaderStageName(ShaderStage stage);

    enum class VertexFormat
    {
        Float2,
        Float3,
        Float4,
    };

    enum class IndexFormat
    {
        UInt16,
        UInt32,
    };

    enum class CullMode
    {
        None,
        Front,
        Back,
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

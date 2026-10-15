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

    /// Texture slots t0..t7 and sampler slots s0..s3 that every pipeline can read, in both the
    /// vertex and the pixel stage. The HLSL register number is the slot: register(t2) reads
    /// whatever setTexture(2, ...) bound. On Metal the slot is the [[texture(n)]] / [[sampler(n)]]
    /// index, which the shader build keeps equal to the register number.
    inline constexpr uint32_t kMaxTextureSlots = 8;
    inline constexpr uint32_t kMaxSamplerSlots = 4;

    /// Layout rules for the buffer side of buffer/texture copies: every row starts at a multiple
    /// of kTextureCopyRowAlignment bytes and every mip level at a multiple of
    /// kTextureCopyOffsetAlignment. These are D3D12's requirements (D3D12_TEXTURE_DATA_PITCH_ALIGNMENT
    /// and D3D12_TEXTURE_DATA_PLACEMENT_ALIGNMENT); Metal's are looser, so one layout serves both.
    inline constexpr uint32_t kTextureCopyRowAlignment = 256;
    inline constexpr uint32_t kTextureCopyOffsetAlignment = 512;

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
        /// Stored sRGB-encoded; the GPU decodes to linear when sampling (before filtering) and
        /// encodes when rendering. For colour textures such as albedo, never for data such as normals.
        RGBA8_UNORM_SRGB,
        BGRA8_UNORM,
    };

    [[nodiscard]] uint32_t bytesPerPixel(Format format);

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

    /// Every texture can also be the source or destination of a copy.
    enum class TextureUsage : uint32_t
    {
        None         = 0,
        Sampled      = 1 << 0,
        RenderTarget = 1 << 1,
    };

    [[nodiscard]] constexpr TextureUsage operator|(TextureUsage a, TextureUsage b)
    {
        return static_cast<TextureUsage>(static_cast<uint32_t>(a) | static_cast<uint32_t>(b));
    }

    [[nodiscard]] constexpr bool hasFlag(TextureUsage flags, TextureUsage flag)
    {
        return (static_cast<uint32_t>(flags) & static_cast<uint32_t>(flag)) != 0;
    }

    /// How the GPU is about to use a resource. A barrier moves a resource between states; on
    /// D3D12 this is a transition barrier, on Metal (which tracks hazards itself) it only
    /// updates the tracked state. ShaderRead is the state for sampled textures.
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

        /// D3D12: load PIX's GPU capturer before the device is created, so PIX can attach to the
        /// running editor and captures can be triggered from inside it. Needs PIX installed.
        /// The debug layer is left off in this mode so it cannot interfere with captures.
        bool pixCapture = false;
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
        Extent2D size;               // Pixels, of mip level 0.
        Format format = Format::Unknown;
        /// 1 for no mipmaps; at most fullMipCount(size).
        uint32_t mipLevels = 1;
        TextureUsage usage = TextureUsage::Sampled;
        std::string debugName{};
    };

    /// Number of levels in a complete mip chain down to 1x1: floor(log2(max(width, height))) + 1.
    [[nodiscard]] uint32_t fullMipCount(Extent2D size);

    /// Size of a mip level. Each level halves the previous one, rounding down, and never goes below 1.
    [[nodiscard]] Extent2D mipSize(Extent2D size, uint32_t mipLevel);

    enum class Filter
    {
        Nearest,
        Linear,
    };

    /// What a sampler returns for texture coordinates outside [0, 1].
    enum class AddressMode
    {
        Repeat,
        MirroredRepeat,
        ClampToEdge,
        ClampToBorder,
    };

    /// The three colours both D3D12 static samplers and Metal support for ClampToBorder.
    enum class BorderColor
    {
        TransparentBlack,
        OpaqueBlack,
        OpaqueWhite,
    };

    struct SamplerDesc
    {
        Filter minFilter = Filter::Linear;    // Texture minified: more texels than pixels.
        Filter magFilter = Filter::Linear;    // Texture magnified: more pixels than texels.
        Filter mipFilter = Filter::Linear;    // Between mip levels. Linear is trilinear filtering.
        AddressMode addressU = AddressMode::Repeat;
        AddressMode addressV = AddressMode::Repeat;
        AddressMode addressW = AddressMode::Repeat;
        BorderColor borderColor = BorderColor::TransparentBlack;
        /// 1 disables anisotropic filtering; at most 16. Above 1, the three filters above are
        /// ignored and treated as Linear, because D3D12 has a single anisotropic filter mode.
        uint32_t maxAnisotropy = 1;
        /// The mip levels the sampler may choose from. maxLod = 0 forces the most detailed level.
        float minLod = 0.0f;
        float maxLod = 1000.0f;
        std::string debugName{};
    };
}

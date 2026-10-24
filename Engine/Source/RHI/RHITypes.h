#pragma once

#include "Core/Handle.h"
#include "Core/Platform.h"
#include "Platform/Window.h"

#include <cstdint>
#include <string>

namespace arc::rhi
{
    class Buffer;
    class Sampler;
    class Texture;

    /// Non-owning references to resources created by a Device. The owner holds the unique_ptr; any
    /// other system keeps a handle and resolves it with Device::find(), which returns nullptr once
    /// the resource has been destroyed.
    using BufferHandle = Handle<Buffer>;
    using TextureHandle = Handle<Texture>;
    using SamplerHandle = Handle<Sampler>;

    /// How many frames the CPU may record ahead of the GPU. Per-frame resources (command
    /// allocators, upload memory) are created this many times and reused round-robin.
    inline constexpr uint32_t kMaxFramesInFlight = 2;

    /// Shader resource slots t0..t15 and sampler slots s0..s3 that every pipeline can read, in both
    /// the vertex and the pixel stage. A t slot holds a texture (setTexture) or a structured buffer
    /// (setBuffer). The HLSL register number is the slot: register(t2) reads whatever was bound to
    /// slot 2. On Metal the slot is the [[texture(n)]] / [[sampler(n)]] index, which the shader build
    /// keeps equal to the register number.
    inline constexpr uint32_t kMaxTextureSlots = 16;
    inline constexpr uint32_t kMaxSamplerSlots = 4;

    /// Constant buffer slots b0..b3, bound with CommandList::setConstants(). By convention b0 holds
    /// per-view data (camera), b1 per-draw data (transform), b2 per-material data and b3 per-frame
    /// lighting. On Metal the slot is the [[buffer(n)]] index, again kept equal to the HLSL register
    /// by the shader build.
    inline constexpr uint32_t kMaxConstantSlots = 4;

    /// The lowest t slot a structured buffer may use. On Metal a structured buffer in t<n> becomes
    /// [[buffer(n)]], which shares its numbering with the constant buffers, so t0..t3 would collide
    /// with b0..b3.
    inline constexpr uint32_t kFirstStructuredBufferSlot = kMaxConstantSlots;

    /// Constant data is placed at multiples of this many bytes (D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT),
    /// which also satisfies Metal's buffer offset rules.
    inline constexpr uint32_t kConstantBufferAlignment = 256;

    /// The largest cbuffer HLSL allows: 4096 16-byte rows.
    inline constexpr uint32_t kMaxConstantBufferSize = 64 * 1024;

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
        /// Four 16-bit floats: linear colour above 1, for HDR render targets. 8 bytes a pixel.
        RGBA16_FLOAT,
        /// 32-bit float depth, no stencil. For DepthStencil textures only: sampling depth needs a
        /// typeless resource with a separate shader view on D3D12, which nothing uses yet.
        D32_FLOAT,
    };

    [[nodiscard]] uint32_t bytesPerPixel(Format format);
    [[nodiscard]] bool isDepthFormat(Format format);

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
        /// Read by shaders as a StructuredBuffer<T> in a t slot, bound with CommandList::setBuffer().
        /// BufferDesc::stride is sizeof(T).
        Structured = 1 << 3,
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
        /// A depth target for render passes. Requires a depth format, and excludes the other usages.
        DepthStencil = 1 << 2,
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
        /// A depth target being tested and written by a render pass.
        DepthWrite,
        Present,
    };

    [[nodiscard]] const char* resourceStateName(ResourceState state);

    struct BufferDesc
    {
        uint64_t size = 0;           // Bytes.
        BufferUsage usage = BufferUsage::None;
        /// Bytes per element of a Structured buffer, a multiple of 4 that divides `size`. Unused otherwise.
        uint32_t stride = 0;
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

    /// How a depth test compares the incoming fragment's depth with the stored one; the fragment
    /// passes when `incoming <op> stored` holds.
    enum class CompareOp
    {
        Never,
        Less,
        LessEqual,
        Equal,
        GreaterEqual,
        Greater,
        NotEqual,
        Always,
    };

    /// How a pipeline combines its output with the colour already in the target.
    enum class BlendMode
    {
        /// Overwrites the target.
        Opaque,
        /// Straight (non-premultiplied) alpha: rgb = src.rgb * src.a + dst.rgb * (1 - src.a). The
        /// target's alpha becomes src.a + dst.a * (1 - src.a).
        AlphaBlend,
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

    enum class TextureType
    {
        Texture2D,
        /// Six square faces, sampled with a direction (TextureCube in HLSL). Faces are array layers
        /// in the order +X, -X, +Y, -Y, +Z, -Z, as on D3D12 and Metal.
        Cube,
    };

    struct TextureDesc
    {
        Extent2D size;               // Pixels, of mip level 0. Square for a cube.
        Format format = Format::Unknown;
        /// 1 for no mipmaps; at most fullMipCount(size).
        uint32_t mipLevels = 1;
        TextureUsage usage = TextureUsage::Sampled;
        TextureType type = TextureType::Texture2D;
        std::string debugName{};
    };

    /// 6 for a cube, 1 otherwise.
    [[nodiscard]] constexpr uint32_t arrayLayerCount(const TextureDesc& desc)
    {
        return desc.type == TextureType::Cube ? 6 : 1;
    }

    /// Mip levels of all array layers numbered one after the other, layer by layer, as D3D12 numbers
    /// subresources: the index of (mip, layer) is mip + layer * mipLevels.
    [[nodiscard]] constexpr uint32_t subresourceIndex(const TextureDesc& desc, uint32_t mipLevel, uint32_t arrayLayer)
    {
        return mipLevel + arrayLayer * desc.mipLevels;
    }

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

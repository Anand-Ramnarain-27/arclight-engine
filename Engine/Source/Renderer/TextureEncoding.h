#pragma once

#include "Renderer/Image.h"
#include "RHI/RHITypes.h"

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace arc
{
    /// A texture as the GPU stores it: every mip level in one format, level 0 first, each level's rows
    /// (of pixels, or of 4x4 blocks for a block-compressed format) tightly packed, top row first.
    struct EncodedTexture
    {
        Extent2D size;   // Pixels, of level 0.
        rhi::Format format = rhi::Format::Unknown;
        std::vector<std::vector<std::byte>> levels;

        [[nodiscard]] bool empty() const { return levels.empty(); }
        [[nodiscard]] uint64_t byteSize() const;
    };

    /// How textures are cooked. Normal maps become BC5 and single-channel masks BC4 whichever
    /// compressed choice is made, since those formats are made for them.
    enum class TextureCompression : uint8_t
    {
        /// RGBA8: 4 bytes a pixel, exactly the source.
        None,
        /// BC7 for colour and packed data: 1 byte a pixel, close to the source.
        Quality,
        /// BC1 for opaque colour and packed data, BC3 for colour with alpha: half a byte a pixel
        /// for opaque images, with visible banding in smooth gradients.
        Small,
    };

    [[nodiscard]] const char* textureCompressionName(TextureCompression compression);

    /// What an image holds, which decides the channels a compressed format must keep.
    enum class TextureContent : uint8_t
    {
        /// Colour, with alpha that matters if any pixel is not opaque.
        Color,
        /// Several data channels packed together, such as glTF's metallic-roughness (green and blue).
        Data,
        /// A tangent-space normal in red and green; blue is rebuilt in the shader.
        Normal,
        /// One value in red, such as ambient occlusion.
        Mask,
    };

    /// The format to cook an image to. Falls back to RGBA8 when `size` is not a whole number of
    /// 4x4 blocks, which D3D12 requires of a block-compressed texture's level 0.
    [[nodiscard]] rhi::Format chooseTextureFormat(TextureContent content, ColorSpace colorSpace, bool hasAlpha,
                                                  TextureCompression compression, Extent2D size);

    /// True if any pixel's alpha is below 255.
    [[nodiscard]] bool hasTransparency(const Image& image);

    /// Encodes a mip chain (level i the size of rhi::mipSize(levels[0].size, i)) to `format`, which
    /// must be RGBA8, RGBA8 sRGB or a block-compressed format. Blocks are encoded on every hardware
    /// thread. The colour space only steers the encoder's error metric: sRGB formats weigh errors
    /// the way the eye sees them, linear ones channel by channel.
    [[nodiscard]] EncodedTexture encodeTexture(std::span<const Image> levels, rhi::Format format);

    /// Decodes back to RGBA8 pixels, one Image a level, as the GPU would sample them before any
    /// sRGB conversion: for devices without block compression, and for tests. BC4 decodes to
    /// (r, 0, 0, 255) and BC5 to (r, g, 0, 255), as sampling returns them.
    [[nodiscard]] std::vector<Image> decodeTexture(const EncodedTexture& texture);
}

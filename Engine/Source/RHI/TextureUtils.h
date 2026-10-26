#pragma once

#include "RHI/RHITypes.h"

#include <cstddef>
#include <span>
#include <vector>

namespace arc::rhi
{
    class Device;
    class Texture;

    /// Where each subresource (mip level of an array layer) of a texture goes in a buffer used to
    /// copy it to or from the GPU, following the kTextureCopy*Alignment rules. Levels are indexed
    /// by subresourceIndex(). Rows are padded, so a level's bytes in the buffer are not tightly
    /// packed.
    struct TextureCopyLayout
    {
        struct Level
        {
            uint64_t offset = 0;      // Bytes from the start of the buffer.
            uint32_t rowPitch = 0;    // Bytes from one row to the next, including padding.
            uint32_t rowBytes = 0;    // Bytes of pixel data in a row.
            /// Rows of pixels, or of 4x4 blocks for a block-compressed format.
            uint32_t rowCount = 0;
            Extent2D size;            // Pixels.
        };

        std::vector<Level> levels;
        uint64_t totalSize = 0;       // Bytes.
    };

    [[nodiscard]] TextureCopyLayout computeTextureCopyLayout(const TextureDesc& desc);

    /// Fills every subresource of a texture through a temporary Upload buffer and leaves it in the
    /// ShaderRead state (Common if it is not Sampled). levels[subresourceIndex(desc, mip, layer)]
    /// holds that mip level of that layer with tightly packed rows (of blocks, for a block-compressed
    /// format), top row first: a 2D texture's
    /// mips in order, or each cube face's mips in turn. Blocks until the copy has completed.
    /// Returns false and logs on failure.
    [[nodiscard]] bool uploadTexture(Device& device, Texture& texture, std::span<const std::span<const std::byte>> levels);

    /// Reads one mip level of one array layer back with tightly packed rows. Blocks until the copy
    /// has completed and leaves the texture in the state it was in. Returns an empty vector and logs
    /// on failure.
    [[nodiscard]] std::vector<std::byte> readbackTexture(Device& device, Texture& texture, uint32_t mipLevel,
                                                         uint32_t arrayLayer = 0);

    /// Uploads a test pattern to a mipmapped texture whose rows need padding, reads every level
    /// back and compares. Exercises the copy layout, texture copies and barriers. Returns true if
    /// the bytes match.
    [[nodiscard]] bool verifyTextureRoundTrip(Device& device);
}

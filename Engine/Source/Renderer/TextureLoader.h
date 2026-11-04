#pragma once

#include "Renderer/Image.h"
#include "Renderer/TextureEncoding.h"

#include <memory>
#include <span>
#include <string>

namespace arc
{
    namespace rhi
    {
        class Device;
        class Texture;
    }

    /// Creates a sampled texture from a set of mip levels: level i must be rhi::mipSize(levels[0].size, i).
    /// sRGB images get an sRGB format, so the GPU decodes them to linear before filtering.
    /// Returns nullptr and logs on failure.
    [[nodiscard]] std::unique_ptr<rhi::Texture> createTexture(rhi::Device& device, std::span<const Image> levels,
                                                              ColorSpace colorSpace, std::string debugName);

    /// Creates a sampled texture from encoded levels. A block-compressed texture is decoded to RGBA8
    /// first on a device without block compression. Returns nullptr and logs on failure.
    [[nodiscard]] std::unique_ptr<rhi::Texture> createTexture(rhi::Device& device, const EncodedTexture& texture,
                                                              std::string debugName);

    /// Loads an image file, builds its full mip chain on the CPU and uploads it. Returns nullptr and
    /// logs on failure.
    [[nodiscard]] std::unique_ptr<rhi::Texture> loadTexture(rhi::Device& device, const std::string& path,
                                                            ColorSpace colorSpace);
}

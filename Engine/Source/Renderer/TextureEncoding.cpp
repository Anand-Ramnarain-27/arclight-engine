#include "Renderer/TextureEncoding.h"

#include "Core/Assert.h"

#include <bc7decomp.h>
#include <bc7enc.h>
#include <rgbcx.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cstring>
#include <mutex>
#include <thread>

namespace arc
{
    namespace
    {
        // Higher levels search more endpoint candidates. These sit where quality has mostly stopped
        // improving and encoding is still a few seconds for a 2048 px texture.
        constexpr uint32_t kBc1Level = 10;
        constexpr uint32_t kBc3Level = 10;

        // Both encoders build lookup tables once, before any thread encodes.
        void initEncoders()
        {
            static std::once_flag once;
            std::call_once(once, [] {
                rgbcx::init(rgbcx::bc1_approx_mode::cBC1Ideal);
                bc7enc_compress_block_init();
            });
        }

        // Calls work(i) for every i in [0, count) on all hardware threads, each taking the next index
        // as it finishes one, so threads that land on cheap rows of blocks are not left idle. The job
        // system of Phase 5 will replace this.
        template <typename Work>
        void parallelFor(uint32_t count, const Work& work)
        {
            const uint32_t threads = std::clamp(std::thread::hardware_concurrency(), 1u, std::max(count, 1u));
            std::atomic<uint32_t> next{0};
            const auto run = [&] {
                for (uint32_t i = next++; i < count; i = next++)
                    work(i);
            };
            std::vector<std::thread> pool;
            pool.reserve(threads - 1);
            for (uint32_t t = 1; t < threads; ++t)
                pool.emplace_back(run);
            run();
            for (std::thread& thread : pool)
                thread.join();
        }

        // The 4x4 block whose top-left pixel is (x, y), as 16 RGBA pixels. Pixels past the image's
        // edge repeat the last row or column, so a partial block's padding does not pull the
        // endpoints toward colours the image does not have.
        std::array<uint8_t, 64> readBlock(const Image& image, uint32_t x, uint32_t y)
        {
            std::array<uint8_t, 64> block{};
            for (uint32_t row = 0; row < 4; ++row)
                for (uint32_t column = 0; column < 4; ++column)
                {
                    const uint32_t sx = std::min(x + column, image.size.width - 1);
                    const uint32_t sy = std::min(y + row, image.size.height - 1);
                    std::memcpy(&block[(row * 4 + column) * 4], &image.pixels[(size_t{sy} * image.size.width + sx) * 4], 4);
                }
            return block;
        }

        void encodeBlock(rhi::Format format, const std::array<uint8_t, 64>& pixels, std::byte* out,
                         const bc7enc_compress_block_params& bc7Params)
        {
            switch (format)
            {
                case rhi::Format::BC1_UNORM:
                case rhi::Format::BC1_UNORM_SRGB:
                    // Three-colour blocks are allowed, but never with their transparent index: BC1
                    // is chosen only for opaque images.
                    rgbcx::encode_bc1(kBc1Level, out, pixels.data(), true, false);
                    break;
                case rhi::Format::BC3_UNORM:
                case rhi::Format::BC3_UNORM_SRGB:
                    rgbcx::encode_bc3_hq(kBc3Level, out, pixels.data());
                    break;
                case rhi::Format::BC4_UNORM:
                    rgbcx::encode_bc4_hq(out, pixels.data());
                    break;
                case rhi::Format::BC5_UNORM:
                    rgbcx::encode_bc5_hq(out, pixels.data());
                    break;
                case rhi::Format::BC7_UNORM:
                case rhi::Format::BC7_UNORM_SRGB:
                    bc7enc_compress_block(out, pixels.data(), &bc7Params);
                    break;
                default:
                    ARC_ASSERT(false, "{} is not a block-compressed format", rhi::formatName(format));
            }
        }

        void decodeBlock(rhi::Format format, const std::byte* block, std::array<uint8_t, 64>& pixels)
        {
            switch (format)
            {
                case rhi::Format::BC1_UNORM:
                case rhi::Format::BC1_UNORM_SRGB:
                    rgbcx::unpack_bc1(block, pixels.data(), true);
                    break;
                case rhi::Format::BC3_UNORM:
                case rhi::Format::BC3_UNORM_SRGB:
                    rgbcx::unpack_bc3(block, pixels.data());
                    break;
                case rhi::Format::BC4_UNORM:
                    for (size_t i = 0; i < 16; ++i)
                    {
                        pixels[i * 4 + 1] = pixels[i * 4 + 2] = 0;
                        pixels[i * 4 + 3] = 255;
                    }
                    rgbcx::unpack_bc4(block, pixels.data());
                    break;
                case rhi::Format::BC5_UNORM:
                    for (size_t i = 0; i < 16; ++i)
                    {
                        pixels[i * 4 + 2] = 0;
                        pixels[i * 4 + 3] = 255;
                    }
                    rgbcx::unpack_bc5(block, pixels.data());
                    break;
                case rhi::Format::BC7_UNORM:
                case rhi::Format::BC7_UNORM_SRGB:
                    bc7decomp::unpack_bc7(block, reinterpret_cast<bc7decomp::color_rgba*>(pixels.data()));
                    break;
                default:
                    ARC_ASSERT(false, "{} is not a block-compressed format", rhi::formatName(format));
            }
        }

        std::vector<std::byte> encodeLevel(const Image& image, rhi::Format format)
        {
            if (!rhi::isBlockCompressed(format))
                return image.pixels;

            bc7enc_compress_block_params bc7Params;
            bc7enc_compress_block_params_init(&bc7Params);
            if (!rhi::isSrgbFormat(format))
                bc7enc_compress_block_params_init_linear_weights(&bc7Params);

            const uint32_t blocksWide = rhi::rowCount(format, image.size.width);
            const uint32_t blockRows = rhi::rowCount(format, image.size.height);
            const uint32_t blockBytes = rhi::bytesPerBlock(format);
            std::vector<std::byte> encoded(size_t{blocksWide} * blockRows * blockBytes);
            parallelFor(blockRows, [&](uint32_t blockRow) {
                for (uint32_t blockColumn = 0; blockColumn < blocksWide; ++blockColumn)
                {
                    std::byte* out = &encoded[(size_t{blockRow} * blocksWide + blockColumn) * blockBytes];
                    encodeBlock(format, readBlock(image, blockColumn * 4, blockRow * 4), out, bc7Params);
                }
            });
            return encoded;
        }
    }

    uint64_t EncodedTexture::byteSize() const
    {
        uint64_t total = 0;
        for (const std::vector<std::byte>& level : levels)
            total += level.size();
        return total;
    }

    const char* textureCompressionName(TextureCompression compression)
    {
        switch (compression)
        {
            case TextureCompression::None:    return "None";
            case TextureCompression::Quality: return "Quality";
            case TextureCompression::Small:   return "Small";
        }
        return "";
    }

    rhi::Format chooseTextureFormat(TextureContent content, ColorSpace colorSpace, bool hasAlpha,
                                    TextureCompression compression, Extent2D size)
    {
        const bool srgb = colorSpace == ColorSpace::Srgb;
        const rhi::Format uncompressed = srgb ? rhi::Format::RGBA8_UNORM_SRGB : rhi::Format::RGBA8_UNORM;
        if (compression == TextureCompression::None || size.width % 4 != 0 || size.height % 4 != 0)
            return uncompressed;

        switch (content)
        {
            case TextureContent::Normal: return rhi::Format::BC5_UNORM;
            case TextureContent::Mask:   return rhi::Format::BC4_UNORM;
            case TextureContent::Color:
            case TextureContent::Data:
                if (compression == TextureCompression::Quality)
                    return srgb ? rhi::Format::BC7_UNORM_SRGB : rhi::Format::BC7_UNORM;
                if (hasAlpha && content == TextureContent::Color)
                    return srgb ? rhi::Format::BC3_UNORM_SRGB : rhi::Format::BC3_UNORM;
                return srgb ? rhi::Format::BC1_UNORM_SRGB : rhi::Format::BC1_UNORM;
        }
        return uncompressed;
    }

    bool hasTransparency(const Image& image)
    {
        for (size_t i = 3; i < image.pixels.size(); i += 4)
            if (image.pixels[i] != std::byte{255})
                return true;
        return false;
    }

    EncodedTexture encodeTexture(std::span<const Image> levels, rhi::Format format)
    {
        ARC_ASSERT(!levels.empty() && !levels[0].empty(), "Nothing to encode");
        ARC_ASSERT(rhi::isBlockCompressed(format) || format == rhi::Format::RGBA8_UNORM ||
                       format == rhi::Format::RGBA8_UNORM_SRGB,
                   "Textures cannot be encoded to {}", rhi::formatName(format));
        initEncoders();

        EncodedTexture texture;
        texture.size = levels[0].size;
        texture.format = format;
        texture.levels.reserve(levels.size());
        for (const Image& level : levels)
            texture.levels.push_back(encodeLevel(level, format));
        return texture;
    }

    std::vector<Image> decodeTexture(const EncodedTexture& texture)
    {
        std::vector<Image> images;
        images.reserve(texture.levels.size());
        for (size_t mip = 0; mip < texture.levels.size(); ++mip)
        {
            Image& image = images.emplace_back();
            image.size = rhi::mipSize(texture.size, static_cast<uint32_t>(mip));
            if (!rhi::isBlockCompressed(texture.format))
            {
                image.pixels = texture.levels[mip];
                continue;
            }

            image.pixels.resize(size_t{image.size.width} * image.size.height * 4);
            const uint32_t blocksWide = rhi::rowCount(texture.format, image.size.width);
            const uint32_t blockRows = rhi::rowCount(texture.format, image.size.height);
            const uint32_t blockBytes = rhi::bytesPerBlock(texture.format);
            ARC_ASSERT(texture.levels[mip].size() == size_t{blocksWide} * blockRows * blockBytes);
            for (uint32_t blockRow = 0; blockRow < blockRows; ++blockRow)
                for (uint32_t blockColumn = 0; blockColumn < blocksWide; ++blockColumn)
                {
                    std::array<uint8_t, 64> pixels{};
                    decodeBlock(texture.format,
                                &texture.levels[mip][(size_t{blockRow} * blocksWide + blockColumn) * blockBytes], pixels);
                    // A partial block's padding is dropped.
                    for (uint32_t row = 0; row < 4 && blockRow * 4 + row < image.size.height; ++row)
                        for (uint32_t column = 0; column < 4 && blockColumn * 4 + column < image.size.width; ++column)
                        {
                            const size_t target = (size_t{blockRow * 4 + row} * image.size.width + blockColumn * 4 + column) * 4;
                            std::memcpy(&image.pixels[target], &pixels[(row * 4 + column) * 4], 4);
                        }
                }
        }
        return images;
    }
}

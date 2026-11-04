#include "Assets/AssetImporters.h"

#include "Core/Log.h"
#include "Platform/VirtualFileSystem.h"
#include "Renderer/GltfImporter.h"
#include "Renderer/LodGenerator.h"

#include <algorithm>
#include <chrono>
#include <optional>

namespace arc
{
    uint32_t importerVersion(AssetType type)
    {
        switch (type)
        {
            case AssetType::Model:   return kModelImporterVersion;
            case AssetType::Texture: return kTextureImporterVersion;
            case AssetType::Unknown: break;
        }
        return 0;
    }

    void scaleModel(ModelData& data, float scale)
    {
        if (scale == 1.0f || !(scale > 0.0f))
            return;
        for (NodeData& node : data.nodes)
            if (node.parent < 0)
            {
                node.translation = node.translation * scale;
                node.scale = node.scale * scale;
            }
        const Mat4 scaling = Mat4::scaling(Vec3{scale});
        for (MeshInstance& instance : data.instances)
            instance.world = scaling * instance.world;
        if (!data.bounds.isEmpty())
        {
            data.bounds.min = data.bounds.min * scale;
            data.bounds.max = data.bounds.max * scale;
        }
    }

    std::vector<TextureContent> imageContents(const ModelData& data)
    {
        std::vector<std::optional<TextureContent>> contents(data.images.size());
        const auto use = [&](int32_t texture, TextureContent content) {
            if (texture < 0 || static_cast<size_t>(texture) >= data.textures.size())
                return;
            const int32_t image = data.textures[static_cast<size_t>(texture)].image;
            if (image < 0 || static_cast<size_t>(image) >= contents.size())
                return;
            std::optional<TextureContent>& current = contents[static_cast<size_t>(image)];
            if (!current || *current == content)
                current = content;
            else
                current = *current == TextureContent::Color || content == TextureContent::Color ? TextureContent::Color
                                                                                                : TextureContent::Data;
        };
        for (const MaterialData& material : data.materials)
        {
            use(material.baseColorTexture, TextureContent::Color);
            use(material.emissiveTexture, TextureContent::Color);
            use(material.metallicRoughnessTexture, TextureContent::Data);
            use(material.normalTexture, TextureContent::Normal);
            use(material.occlusionTexture, TextureContent::Mask);
        }
        std::vector<TextureContent> result(contents.size(), TextureContent::Color);
        for (size_t i = 0; i < contents.size(); ++i)
            result[i] = contents[i].value_or(TextureContent::Color);
        return result;
    }

    std::optional<ModelData> importModelAsset(std::span<const std::byte> source, const std::string& path,
                                              const ModelImportSettings& settings)
    {
        std::optional<ModelData> data = importGltf(source, parentPath(path), std::string(fileNameOf(path)));
        if (!data)
            return std::nullopt;

        scaleModel(*data, settings.scale);
        if (settings.generateLods && settings.lodLevels > 0)
        {
            LodSettings lods;
            lods.reduction = settings.lodReduction;
            lods.maxLevels = settings.lodLevels;
            generateLods(*data, lods);
        }

        // Mips are averaged in each image's own colour space (sRGB colour in linear light, data as it
        // is), then every level is encoded.
        const auto start = std::chrono::steady_clock::now();
        const std::vector<TextureContent> contents = imageContents(*data);
        data->encodedImages.resize(data->images.size());
        data->imageColorSpaces.resize(data->images.size(), ColorSpace::Srgb);
        uint64_t rawBytes = 0;
        uint64_t encodedBytes = 0;
        for (size_t i = 0; i < data->images.size(); ++i)
        {
            Image& image = data->images[i];
            if (image.empty())
                continue;
            const ColorSpace colorSpace = data->imageColorSpaces[i];
            const bool alpha = contents[i] == TextureContent::Color && hasTransparency(image);
            const rhi::Format format =
                chooseTextureFormat(contents[i], colorSpace, alpha, settings.textureCompression, image.size);
            const std::vector<Image> levels = generateMipChain(std::move(image), colorSpace);
            data->encodedImages[i] = encodeTexture(levels, format);
            ARC_LOG_TRACE("'{}' image {}: {}x{} as {}", path, i, levels[0].size.width, levels[0].size.height,
                          rhi::formatName(format));
            for (const Image& level : levels)
                rawBytes += level.pixels.size();
            encodedBytes += data->encodedImages[i].byteSize();
        }
        data->images.assign(data->encodedImages.size(), Image{});
        if (rawBytes > 0)
        {
            const double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
            ARC_LOG_INFO("'{}': textures {:.1f} MB as RGBA8, {:.1f} MB cooked ({}), in {:.1f} s", path,
                         static_cast<double>(rawBytes) / (1024.0 * 1024.0),
                         static_cast<double>(encodedBytes) / (1024.0 * 1024.0),
                         textureCompressionName(settings.textureCompression), seconds);
        }
        return data;
    }

    std::optional<CookedTexture> importTextureAsset(std::span<const std::byte> source, const std::string& path,
                                                    const TextureImportSettings& settings)
    {
        Image image = decodeImage(source, path);
        if (image.empty())
            return std::nullopt;
        const uint32_t maxSize = std::max(settings.maxSize, 1u);
        while (image.size.width > maxSize || image.size.height > maxSize)
            image = downsample(image, settings.colorSpace);

        const rhi::Format format = chooseTextureFormat(TextureContent::Color, settings.colorSpace, hasTransparency(image),
                                                       settings.compression, image.size);
        std::vector<Image> levels;
        if (settings.generateMips)
            levels = generateMipChain(std::move(image), settings.colorSpace);
        else
            levels.push_back(std::move(image));

        CookedTexture texture;
        texture.name = std::string(fileNameOf(path));
        texture.texture = encodeTexture(levels, format);
        return texture;
    }
}

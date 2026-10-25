#include "Assets/AssetImporters.h"

#include "Core/Log.h"
#include "Platform/VirtualFileSystem.h"
#include "Renderer/GltfImporter.h"
#include "Renderer/LodGenerator.h"

#include <algorithm>

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

        // Mips are averaged in each image's own colour space: sRGB colour in linear light, data as it is.
        data->imageMips.resize(data->images.size());
        data->imageColorSpaces.resize(data->images.size(), ColorSpace::Srgb);
        for (size_t i = 0; i < data->images.size(); ++i)
            if (!data->images[i].empty())
                data->imageMips[i] = generateMipChain(std::move(data->images[i]), data->imageColorSpaces[i]);
        data->images.assign(data->imageMips.size(), Image{});
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

        CookedTexture texture;
        texture.name = std::string(fileNameOf(path));
        texture.colorSpace = settings.colorSpace;
        if (settings.generateMips)
            texture.levels = generateMipChain(std::move(image), settings.colorSpace);
        else
            texture.levels.push_back(std::move(image));
        return texture;
    }
}

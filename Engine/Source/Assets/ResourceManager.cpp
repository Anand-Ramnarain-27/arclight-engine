#include "Assets/ResourceManager.h"

#include "Assets/AssetDatabase.h"
#include "Core/Log.h"
#include "Core/Time.h"
#include "RHI/Texture.h"
#include "Renderer/Model.h"
#include "Renderer/TextureLoader.h"

#include <algorithm>

namespace arc
{
    // Defined here, where rhi::Texture is complete, for the unique_ptr.
    TextureAsset::TextureAsset() = default;
    TextureAsset::~TextureAsset() = default;
    TextureAsset::TextureAsset(TextureAsset&&) noexcept = default;
    TextureAsset& TextureAsset::operator=(TextureAsset&&) noexcept = default;

    ResourceManager::ResourceManager(AssetDatabase& database, rhi::Device& device, GeometryPool& geometry)
        : m_database(database)
        , m_device(device)
        , m_geometry(geometry)
    {
    }

    ResourceManager::~ResourceManager() = default;

    std::shared_ptr<const Model> ResourceManager::loadModel(AssetId asset)
    {
        if (const auto cached = m_models.find(asset); cached != m_models.end())
            if (std::shared_ptr<const Model> model = cached->second.lock())
                return model;

        const AssetRecord* record = m_database.find(asset);
        if (!record || record->meta.type != AssetType::Model)
        {
            ARC_LOG_ERROR("Asset {} is not a model", asset.toString());
            return nullptr;
        }
        const Stopwatch timer;
        const std::optional<ModelData> data = m_database.loadModel(asset);
        if (!data)
            return nullptr;
        std::shared_ptr<const Model> model = Model::create(m_device, m_geometry, *data, asset);
        if (!model)
            return nullptr;

        ARC_LOG_INFO("Loaded '{}' in {:.1f} ms: {} triangles, {} materials, {} textures ({:.1f} MB)", record->path,
                     timer.elapsedMs(), model->triangleCount(), model->materials().size(), model->textureCount(),
                     static_cast<double>(model->textureBytes()) / (1024.0 * 1024.0));
        m_models[asset] = model;
        return model;
    }

    std::shared_ptr<const Model> ResourceManager::loadModel(const std::string& path)
    {
        const AssetId asset = m_database.registerPath(path);
        if (!asset)
        {
            ARC_LOG_ERROR("'{}' is not a model file that exists", path);
            return nullptr;
        }
        return loadModel(asset);
    }

    std::shared_ptr<const TextureAsset> ResourceManager::loadTexture(AssetId asset)
    {
        if (const auto cached = m_textures.find(asset); cached != m_textures.end())
            if (std::shared_ptr<const TextureAsset> texture = cached->second.lock())
                return texture;

        const AssetRecord* record = m_database.find(asset);
        if (!record || record->meta.type != AssetType::Texture)
        {
            ARC_LOG_ERROR("Asset {} is not a texture", asset.toString());
            return nullptr;
        }
        std::optional<CookedTexture> cooked = m_database.loadTexture(asset);
        if (!cooked)
            return nullptr;

        auto texture = std::make_shared<TextureAsset>();
        texture->asset = asset;
        texture->name = cooked->name;
        texture->colorSpace = rhi::isSrgbFormat(cooked->texture.format) ? ColorSpace::Srgb : ColorSpace::Linear;
        texture->texture = createTexture(m_device, cooked->texture, cooked->name);
        if (!texture->texture)
            return nullptr;
        m_textures[asset] = texture;
        return texture;
    }

    std::shared_ptr<const TextureAsset> ResourceManager::loadTexture(const std::string& path)
    {
        const AssetId asset = m_database.registerPath(path);
        if (!asset)
        {
            ARC_LOG_ERROR("'{}' is not an image file that exists", path);
            return nullptr;
        }
        return loadTexture(asset);
    }

    std::vector<LoadedResource> ResourceManager::loaded() const
    {
        std::vector<LoadedResource> result;
        const auto add = [&](AssetId asset, AssetType type, long references) {
            if (references == 0)
                return;
            const AssetRecord* record = m_database.find(asset);
            result.push_back({asset, record ? record->path : asset.toString(), type, references});
        };
        for (const auto& [asset, model] : m_models)
            add(asset, AssetType::Model, model.use_count());
        for (const auto& [asset, texture] : m_textures)
            add(asset, AssetType::Texture, texture.use_count());
        std::sort(result.begin(), result.end(), [](const LoadedResource& a, const LoadedResource& b) { return a.path < b.path; });
        return result;
    }

    std::vector<std::shared_ptr<const Model>> ResourceManager::loadedModels() const
    {
        std::vector<std::shared_ptr<const Model>> result;
        for (const auto& [asset, weak] : m_models)
            if (std::shared_ptr<const Model> model = weak.lock())
                result.push_back(std::move(model));
        return result;
    }
}

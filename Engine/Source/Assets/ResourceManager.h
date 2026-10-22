#pragma once

#include "Assets/AssetMeta.h"

#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace arc
{
    class AssetDatabase;
    class GeometryPool;
    class Model;

    namespace rhi
    {
        class Device;
        class Texture;
    }

    /// A texture asset on the GPU.
    struct TextureAsset
    {
        AssetId asset;
        std::string name;
        ColorSpace colorSpace = ColorSpace::Srgb;
        std::unique_ptr<rhi::Texture> texture;

        TextureAsset();
        ~TextureAsset();
        TextureAsset(TextureAsset&&) noexcept;
        TextureAsset& operator=(TextureAsset&&) noexcept;
    };

    struct LoadedResource
    {
        AssetId asset;
        std::string path;
        AssetType type = AssetType::Unknown;
        /// How many owners hold it: MeshComponents, for a model.
        long references = 0;
    };

    /// Loads assets once and shares them. Whoever asks gets a shared_ptr, which is the reference
    /// count: the resource is released, and its GPU memory freed once the frames using it finish,
    /// when the last owner lets go. The cache only holds resources weakly, so it never keeps one
    /// alive by itself.
    ///
    /// A re-import changes what the next load returns; resources already loaded stay as they are
    /// until every owner has released them.
    ///
    /// Main thread only, outside rendering: loading uploads to the GPU.
    class ResourceManager
    {
    public:
        ResourceManager(AssetDatabase& database, rhi::Device& device, GeometryPool& geometry);
        ~ResourceManager();
        ResourceManager(const ResourceManager&) = delete;
        ResourceManager& operator=(const ResourceManager&) = delete;

        /// Null after logging if the asset is not a model or cannot be loaded.
        [[nodiscard]] std::shared_ptr<const Model> loadModel(AssetId asset);
        /// A virtual or native path; registered with the database if it is not known yet.
        [[nodiscard]] std::shared_ptr<const Model> loadModel(const std::string& path);

        [[nodiscard]] std::shared_ptr<const TextureAsset> loadTexture(AssetId asset);
        [[nodiscard]] std::shared_ptr<const TextureAsset> loadTexture(const std::string& path);

        /// Resources still alive, sorted by path.
        [[nodiscard]] std::vector<LoadedResource> loaded() const;
        [[nodiscard]] std::vector<std::shared_ptr<const Model>> loadedModels() const;

    private:
        AssetDatabase& m_database;
        rhi::Device& m_device;
        GeometryPool& m_geometry;
        std::unordered_map<AssetId, std::weak_ptr<const Model>> m_models;
        std::unordered_map<AssetId, std::weak_ptr<const TextureAsset>> m_textures;
    };
}

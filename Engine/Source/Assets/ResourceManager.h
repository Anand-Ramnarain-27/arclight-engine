#pragma once

#include "Assets/AssetMeta.h"

#include <atomic>
#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace arc
{
    class AssetDatabase;
    struct AssetRecord;
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

    /// A model being loaded in the background, shared by everyone who asked for the same asset.
    /// Changes only in ResourceManager::update() and the other main-thread calls that finish loads,
    /// so it is read on the main thread.
    class ModelRequest
    {
    public:
        enum class State : uint8_t
        {
            Loading,
            Ready,
            Failed,
        };

        [[nodiscard]] AssetId asset() const { return m_asset; }
        [[nodiscard]] State state() const { return m_state; }
        [[nodiscard]] bool finished() const { return m_state != State::Loading; }
        /// Null unless Ready.
        [[nodiscard]] const std::shared_ptr<const Model>& model() const { return m_model; }

    private:
        friend class ResourceManager;
        AssetId m_asset;
        State m_state = State::Loading;
        std::shared_ptr<const Model> m_model;
    };

    /// Loads assets once and shares them. Whoever asks gets a shared_ptr, which is the reference
    /// count: the resource is released, and its GPU memory freed once the frames using it finish,
    /// when the last owner lets go. The cache only holds resources weakly, so it never keeps one
    /// alive by itself.
    ///
    /// A re-import changes what the next load returns; resources already loaded stay as they are
    /// until every owner has released them.
    ///
    /// Models can also be loaded in the background with requestModel(): reading or cooking the
    /// asset runs as a background job, and update() uploads the result on the main thread once that
    /// is done. Every request for the same asset shares one load, and a synchronous loadModel() of
    /// an asset loading in the background waits for that load rather than starting another.
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

        /// Never null: a request that failed already when the asset is not a model. Ready already
        /// when the model is loaded. Without an active job system the model loads before this returns.
        [[nodiscard]] std::shared_ptr<const ModelRequest> requestModel(AssetId asset);
        [[nodiscard]] std::shared_ptr<const ModelRequest> requestModel(const std::string& path);

        /// Uploads the background loads whose CPU work is done, in the order they were requested,
        /// until `budgetMilliseconds` have been spent; at least one per call, so a large model does
        /// not hold the rest up forever. Loads that nobody holds a request for any more are dropped
        /// without uploading. Once a frame, before anything looks at its requests.
        void update(double budgetMilliseconds);

        /// Waits for every background load and finishes it. Before cooking on the main thread, which
        /// must not race a worker cooking the same asset.
        void finishLoads();

        /// Background loads not finished yet, including those whose upload is still to come.
        [[nodiscard]] size_t loadsInFlight() const { return m_pending.size(); }
        /// Their paths, in request order.
        [[nodiscard]] std::vector<std::string> loadingPaths() const;
        /// Changes whenever a background load finishes, so whoever holds requests knows when to look
        /// at them again.
        [[nodiscard]] uint64_t finishedLoads() const { return m_finishedLoads; }

        [[nodiscard]] std::shared_ptr<const TextureAsset> loadTexture(AssetId asset);
        [[nodiscard]] std::shared_ptr<const TextureAsset> loadTexture(const std::string& path);

        /// Resources still alive, sorted by path.
        [[nodiscard]] std::vector<LoadedResource> loaded() const;
        [[nodiscard]] std::vector<std::shared_ptr<const Model>> loadedModels() const;

    private:
        struct PendingModel;

        [[nodiscard]] std::shared_ptr<const Model> createModel(const AssetRecord& record, const ModelData& data,
                                                               double loadMilliseconds);
        void finish(PendingModel& pending);
        [[nodiscard]] PendingModel* findPending(AssetId asset) const;
        void erasePending(const PendingModel& pending);

        AssetDatabase& m_database;
        rhi::Device& m_device;
        GeometryPool& m_geometry;
        std::unordered_map<AssetId, std::weak_ptr<const Model>> m_models;
        std::unordered_map<AssetId, std::weak_ptr<const TextureAsset>> m_textures;
        // In request order. Each is owned here until it is finished, and its job must have ended by then.
        std::vector<std::unique_ptr<PendingModel>> m_pending;
        uint64_t m_finishedLoads = 0;
        // Set on destruction, so the jobs that have not started skip their work.
        std::atomic<bool> m_cancelled{false};
    };
}

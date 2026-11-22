#include "Assets/ResourceManager.h"

#include "Assets/AssetDatabase.h"
#include "Core/JobSystem.h"
#include "Core/Log.h"
#include "Core/Profiler.h"
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

    struct ResourceManager::PendingModel
    {
        std::shared_ptr<ModelRequest> request;
        // A copy of the asset's record, which the job cooks with, so the database's own records
        // stay main thread only.
        AssetRecord record;
        std::optional<ModelData> data;
        double loadMilliseconds = 0.0;
        JobCounter counter;
    };

    ResourceManager::~ResourceManager()
    {
        m_cancelled.store(true, std::memory_order_relaxed);
        if (JobSystem* jobs = activeJobSystem())
            for (const std::unique_ptr<PendingModel>& pending : m_pending)
                jobs->wait(pending->counter);
    }

    std::shared_ptr<const Model> ResourceManager::loadModel(AssetId asset)
    {
        ARC_PROFILE_SCOPE("Load model");
        if (const auto cached = m_models.find(asset); cached != m_models.end())
            if (std::shared_ptr<const Model> model = cached->second.lock())
                return model;

        if (PendingModel* pending = findPending(asset))
        {
            const std::shared_ptr<ModelRequest> request = pending->request;
            finish(*pending);
            return request->model();
        }

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
        return createModel(*record, *data, timer.elapsedMs());
    }

    std::shared_ptr<const Model> ResourceManager::createModel(const AssetRecord& record, const ModelData& data,
                                                              double loadMilliseconds)
    {
        ARC_PROFILE_SCOPE("Upload model");
        const Stopwatch timer;
        std::shared_ptr<const Model> model = Model::create(m_device, m_geometry, data, record.meta.id);
        if (!model)
            return nullptr;
        ARC_LOG_INFO("Loaded '{}' in {:.1f} ms + {:.1f} ms to upload: {} triangles, {} materials, {} textures ({:.1f} MB)",
                     record.path, loadMilliseconds, timer.elapsedMs(), model->triangleCount(), model->materials().size(),
                     model->textureCount(), static_cast<double>(model->textureBytes()) / (1024.0 * 1024.0));
        m_models[record.meta.id] = model;
        return model;
    }

    std::shared_ptr<const ModelRequest> ResourceManager::requestModel(AssetId asset)
    {
        if (PendingModel* pending = findPending(asset))
            return pending->request;

        auto request = std::make_shared<ModelRequest>();
        request->m_asset = asset;
        if (const auto cached = m_models.find(asset); cached != m_models.end())
            if (std::shared_ptr<const Model> model = cached->second.lock())
            {
                request->m_state = ModelRequest::State::Ready;
                request->m_model = std::move(model);
                return request;
            }

        const AssetRecord* record = m_database.find(asset);
        if (!record || record->meta.type != AssetType::Model)
        {
            ARC_LOG_ERROR("Asset {} is not a model", asset.toString());
            request->m_state = ModelRequest::State::Failed;
            return request;
        }
        JobSystem* jobs = activeJobSystem();
        if (!jobs)
        {
            request->m_model = loadModel(asset);
            request->m_state = request->m_model ? ModelRequest::State::Ready : ModelRequest::State::Failed;
            return request;
        }

        auto pending = std::make_unique<PendingModel>();
        pending->request = request;
        pending->record = *record;
        PendingModel& work = *pending;
        m_pending.push_back(std::move(pending));
        jobs->runBackground(work.counter, [this, &work] {
            if (m_cancelled.load(std::memory_order_relaxed))
                return;
            ARC_PROFILE_SCOPE("Load model in the background");
            const Stopwatch timer;
            work.data = m_database.loadModel(work.record);
            work.loadMilliseconds = timer.elapsedMs();
        });
        return request;
    }

    std::shared_ptr<const ModelRequest> ResourceManager::requestModel(const std::string& path)
    {
        const AssetId asset = m_database.registerPath(path);
        if (!asset)
        {
            ARC_LOG_ERROR("'{}' is not a model file that exists", path);
            auto request = std::make_shared<ModelRequest>();
            request->m_state = ModelRequest::State::Failed;
            return request;
        }
        return requestModel(asset);
    }

    void ResourceManager::finish(PendingModel& pending)
    {
        if (!pending.counter.done())
        {
            ARC_PROFILE_SCOPE("Wait for a background load");
            activeJobSystem()->wait(pending.counter);
        }
        m_database.finishLoad(pending.record);
        ModelRequest& request = *pending.request;
        // The only owner left is this list: whoever asked has gone, as when a scene is closed while
        // it loads, so the upload would be wasted.
        if (pending.data && pending.request.use_count() > 1)
            request.m_model = createModel(pending.record, *pending.data, pending.loadMilliseconds);
        request.m_state = request.m_model ? ModelRequest::State::Ready : ModelRequest::State::Failed;
        ++m_finishedLoads;
        erasePending(pending);
    }

    void ResourceManager::update(double budgetMilliseconds)
    {
        if (m_pending.empty())
            return;
        ARC_PROFILE_SCOPE("Finish background loads");
        const Stopwatch timer;
        bool finishedOne = false;
        for (size_t i = 0; i < m_pending.size();)
        {
            if (finishedOne && timer.elapsedMs() >= budgetMilliseconds)
                return;
            if (!m_pending[i]->counter.done())
            {
                ++i;
                continue;
            }
            // finish() removes it, so the next one moves into slot i.
            finish(*m_pending[i]);
            finishedOne = true;
        }
    }

    void ResourceManager::finishLoads()
    {
        while (!m_pending.empty())
            finish(*m_pending.front());
    }

    std::vector<std::string> ResourceManager::loadingPaths() const
    {
        std::vector<std::string> paths;
        paths.reserve(m_pending.size());
        for (const std::unique_ptr<PendingModel>& pending : m_pending)
            paths.push_back(pending->record.path);
        return paths;
    }

    ResourceManager::PendingModel* ResourceManager::findPending(AssetId asset) const
    {
        for (const std::unique_ptr<PendingModel>& pending : m_pending)
            if (pending->record.meta.id == asset)
                return pending.get();
        return nullptr;
    }

    void ResourceManager::erasePending(const PendingModel& pending)
    {
        std::erase_if(m_pending, [&](const std::unique_ptr<PendingModel>& entry) { return entry.get() == &pending; });
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
        ARC_PROFILE_SCOPE("Load texture");
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

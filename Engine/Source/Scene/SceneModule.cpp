#include "Scene/SceneModule.h"

#include "Application/Application.h"
#include "Core/Log.h"
#include "Renderer/GltfImporter.h"
#include "Renderer/Model.h"
#include "Scene/MeshComponent.h"
#include "Scene/ModelInstantiation.h"

#include <chrono>

namespace arc
{
    SceneModule::SceneModule(Application& app)
        : Module(app)
        , m_scene(std::make_unique<Scene>())
    {
    }

    // Defined here, where Model is complete, for the shared_ptr and weak_ptr members.
    SceneModule::~SceneModule() = default;

    bool SceneModule::init()
    {
        return m_meshRenderer.init(app().renderer());
    }

    void SceneModule::shutdown()
    {
        // The Application waits for the GPU before shutting modules down, and the device must
        // still exist when the models' buffers and textures are released.
        m_drawItems.clear();
        m_scene = std::make_unique<Scene>();
        m_models.clear();
        m_meshRenderer.shutdown();
    }

    void SceneModule::update(const FrameTime& time)
    {
        m_scene->update(time);
    }

    void SceneModule::preRender()
    {
        m_scene->flushDestroyed();
    }

    void SceneModule::render()
    {
        m_drawItems.clear();
        m_scene->forEachActive([this](GameObject& object) {
            for (const std::unique_ptr<Component>& component : object.components())
            {
                if (!component->enabled || &component->type() != &MeshComponent::staticType())
                    continue;
                const auto& mesh = static_cast<const MeshComponent&>(*component);
                if (mesh.hasMesh())
                    m_drawItems.push_back({.model = mesh.model().get(),
                                           .mesh = mesh.meshIndex(),
                                           .world = object.transform().worldMatrix(),
                                           .debugName = object.name()});
            }
        });
        m_drawnMeshCount = static_cast<uint32_t>(m_drawItems.size());

        Renderer& renderer = app().renderer();
        if (m_drawItems.empty() || !renderer.isFrameActive() || !renderer.beginScenePass("Scene"))
            return;
        m_meshRenderer.draw(renderer, m_drawItems, debugView);
        renderer.endScenePass();
    }

    std::shared_ptr<const Model> SceneModule::loadModel(const std::string& path)
    {
        if (const auto cached = m_models.find(path); cached != m_models.end())
            if (std::shared_ptr<const Model> model = cached->second.lock())
                return model;

        const Clock::time_point start = Clock::now();
        const std::optional<ModelData> data = importGltf(path);
        if (!data)
            return nullptr;
        std::shared_ptr<const Model> model = Model::create(app().renderer().device(), *data);
        if (!model)
            return nullptr;

        const double milliseconds = std::chrono::duration<double, std::milli>(Clock::now() - start).count();
        ARC_LOG_INFO("Loaded '{}': {} nodes, {} triangles, {} materials, {} textures in {:.0f} ms", model->name(),
                     model->nodes().size(), model->triangleCount(), model->materials().size(), model->textureCount(),
                     milliseconds);
        m_models[path] = model;
        return model;
    }

    GameObject* SceneModule::importModel(const std::string& path, GameObject* parent)
    {
        const std::shared_ptr<const Model> model = loadModel(path);
        return model ? &instantiateModel(*m_scene, model, parent) : nullptr;
    }

    std::vector<std::shared_ptr<const Model>> SceneModule::loadedModels() const
    {
        std::vector<std::shared_ptr<const Model>> models;
        for (const auto& [path, weak] : m_models)
            if (std::shared_ptr<const Model> model = weak.lock())
                models.push_back(std::move(model));
        return models;
    }
}

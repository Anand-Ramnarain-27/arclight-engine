#include "Scene/SceneModule.h"

#include "Application/Application.h"
#include "Core/Log.h"
#include "Assets/AssetModule.h"
#include "Assets/ResourceManager.h"
#include "Platform/FileSystem.h"
#include "Scene/SceneSerializer.h"
#include "Renderer/Model.h"
#include "Renderer/ToneMapper.h"
#include "Scene/LightComponent.h"
#include "Scene/MeshComponent.h"
#include "Scene/ModelInstantiation.h"

#include <cmath>

namespace arc
{
    SceneModule::SceneModule(Application& app, AssetModule& assets)
        : Module(app)
        , m_assets(assets)
        , m_scene(std::make_unique<Scene>())
    {
    }

    // Defined here, where Model is complete, for the shared_ptr and weak_ptr members.
    SceneModule::~SceneModule() = default;

    bool SceneModule::init()
    {
        return m_meshRenderer.init(app().renderer()) && m_skybox.init(app().renderer());
    }

    void SceneModule::shutdown()
    {
        // The Application waits for the GPU before shutting modules down, and the device must
        // still exist when the models' buffers and textures are released.
        m_renderList.clear();
        m_scene = std::make_unique<Scene>();
        m_skybox.shutdown();
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

    uint32_t SceneModule::buildCullNode(const GameObject& object)
    {
        const auto index = static_cast<uint32_t>(m_cullNodes.size());
        m_cullNodes.emplace_back().object = &object;

        Aabb bounds;
        uint32_t meshes = 0;
        const Mat4& world = object.transform().worldMatrix();
        for (const std::unique_ptr<Component>& component : object.components())
        {
            if (!component->enabled || &component->type() != &MeshComponent::staticType())
                continue;
            const auto& mesh = static_cast<const MeshComponent&>(*component);
            if (!mesh.hasMesh())
                continue;
            bounds.expand(mesh.localBounds().transformed(world));
            ++meshes;
        }
        for (const GameObject* child : object.children())
        {
            // Inactive objects, and those marked for destruction, are not drawn, nor is anything below them.
            if (!child->isActiveSelf() || !m_scene->find(child->handle()))
                continue;
            const uint32_t childIndex = buildCullNode(*child);
            bounds.expand(m_cullNodes[childIndex].bounds);
            meshes += m_cullNodes[childIndex].subtreeMeshes;
        }

        // Indexed again: the recursion may have moved the vector's storage.
        CullNode& node = m_cullNodes[index];
        node.bounds = bounds;
        node.subtreeMeshes = meshes;
        node.subtreeEnd = static_cast<uint32_t>(m_cullNodes.size());
        return index;
    }

    void SceneModule::addMeshes(const GameObject& object, const Frustum* frustum, const LodPolicy& lods)
    {
        const Mat4& world = object.transform().worldMatrix();
        for (const std::unique_ptr<Component>& component : object.components())
        {
            if (!component->enabled || &component->type() != &MeshComponent::staticType())
                continue;
            const auto& mesh = static_cast<const MeshComponent&>(*component);
            if (!mesh.hasMesh())
                continue;
            if (frustum)
            {
                ++m_cullingStats.meshesTested;
                if (frustum->classify(Obb::fromAabb(mesh.localBounds(), world)) == Containment::Outside)
                {
                    ++m_cullingStats.meshesCulled;
                    continue;
                }
            }
            m_renderList.addMesh(*mesh.model(), mesh.meshIndex(), world, lods, mesh.materialStyles());
        }
    }

    void SceneModule::cullAndCollect(const Frustum& frustum, const LodPolicy& lods)
    {
        m_cullNodes.clear();
        for (const GameObject* root : m_scene->roots())
            if (root->isActiveSelf() && m_scene->find(root->handle()))
                buildCullNode(*root);

        m_cullingStats = {};
        m_cullingStats.objects = static_cast<uint32_t>(m_cullNodes.size());
        // Top-level nodes are the ones the walk below starts from; their subtrees cover every node.
        for (uint32_t j = 0; j < m_cullNodes.size(); j = m_cullNodes[j].subtreeEnd)
            m_cullingStats.meshes += m_cullNodes[j].subtreeMeshes;

        // The nodes are in depth-first order, so a subtree is the run of nodes up to subtreeEnd.
        uint32_t i = 0;
        while (i < m_cullNodes.size())
        {
            const CullNode& node = m_cullNodes[i];
            const Containment containment = frustumCulling ? frustum.classify(node.bounds) : Containment::Inside;
            if (frustumCulling)
                ++m_cullingStats.objectsTested;

            if (containment == Containment::Outside)
            {
                if (node.subtreeMeshes > 0)
                    ++m_cullingStats.subtreesCulled;
                m_cullingStats.meshesCulled += node.subtreeMeshes;
                i = node.subtreeEnd;
            }
            else if (containment == Containment::Inside)
            {
                for (uint32_t j = i; j < node.subtreeEnd; ++j)
                    addMeshes(*m_cullNodes[j].object, nullptr, lods);
                i = node.subtreeEnd;
            }
            else
            {
                addMeshes(*node.object, &frustum, lods);
                ++i;
            }
        }
    }

    void SceneModule::render()
    {
        Renderer& renderer = app().renderer();
        const Camera& camera = renderer.sceneCamera();
        const Extent2D size = renderer.sceneSize();
        const float aspect = size.height > 0 ? static_cast<float>(size.width) / static_cast<float>(size.height) : 1.0f;

        // While frozen, culling and level-of-detail selection keep the camera they had.
        if (!freezeCulling)
        {
            m_cullCamera = camera;
            m_cullAspect = aspect;
        }
        const LodPolicy lods{
            .cameraPosition = m_cullCamera.position,
            .pixelsPerUnit = static_cast<float>(size.height) / (2.0f * std::tan(m_cullCamera.verticalFov * 0.5f)),
            .maxErrorPixels = lodErrorPixels,
            .forcedLevel = forcedLod,
        };

        m_renderList.clear();
        cullAndCollect(m_cullCamera.frustum(m_cullAspect), lods);
        m_renderList.sort(camera.position, camera.forward(), sortDraws);

        m_lights.clear();
        m_sceneLightCount = collectLights(*m_scene, m_lights);
        m_usedDefaultLight = m_sceneLightCount == 0 && lighting.defaultLight;
        if (m_usedDefaultLight)
            m_lights.push_back(defaultSunLight());
        const EnvironmentLighting environment = m_skybox.environmentLighting();
        m_meshRenderer.setLighting(renderer, m_lights, lighting, &environment);
        renderer.toneMapper().passThrough = MeshRenderer::isDataView(debugView);

        m_stats = {};
        const bool drawSky = m_skybox.visible;
        if ((m_renderList.empty() && !drawSky) || !renderer.isFrameActive() || !renderer.beginScenePass("Scene"))
            return;
        const RenderStats opaque = m_meshRenderer.draw(renderer, m_renderList, MeshRenderer::Queue::Opaque, debugView) +
                                   m_meshRenderer.drawOutlines(renderer, m_renderList);
        m_skybox.draw(renderer);
        const RenderStats blended = m_meshRenderer.draw(renderer, m_renderList, MeshRenderer::Queue::Blended, debugView);
        renderer.endScenePass();
        m_stats = opaque + blended;
    }

    std::shared_ptr<const Model> SceneModule::loadModel(const std::string& path)
    {
        return m_assets.resources().loadModel(path);
    }

    std::shared_ptr<const TextureAsset> SceneModule::loadTexture(const std::string& path)
    {
        return m_assets.resources().loadTexture(path);
    }

    GameObject* SceneModule::importModel(const std::string& path, GameObject* parent)
    {
        const std::shared_ptr<const Model> model = loadModel(path);
        return model ? &instantiateModel(*m_scene, model, parent) : nullptr;
    }

    std::vector<std::shared_ptr<const Model>> SceneModule::loadedModels() const
    {
        return m_assets.resources().loadedModels();
    }

    namespace
    {
        std::optional<std::string> readText(const std::string& path)
        {
            const std::optional<std::vector<std::byte>> bytes = readFile(path);
            if (!bytes)
                return std::nullopt;
            return std::string(reinterpret_cast<const char*>(bytes->data()), bytes->size());
        }

        bool writeText(const std::string& path, std::string_view text)
        {
            return writeFile(path, std::as_bytes(std::span(text.data(), text.size())));
        }
    }

    void SceneModule::newScene()
    {
        m_scene->clear();
        m_scenePath.clear();
    }

    bool SceneModule::openScene(const std::string& path)
    {
        const std::optional<std::string> text = readText(path);
        if (!text)
        {
            ARC_LOG_ERROR("Cannot read the scene '{}'", path);
            return false;
        }
        const Stopwatch timer;
        std::string error;
        SceneLoadOptions options;
        options.context.resources = &m_assets.resources();
        // Clearing the old scene would release models the new one uses too, only to load them again.
        const std::vector<std::shared_ptr<const Model>> keepLoaded = loadedModels();
        if (!readScene(*m_scene, *text, options, &error))
        {
            ARC_LOG_ERROR("Cannot open the scene '{}': {}", path, error);
            return false;
        }
        m_scenePath = path;
        ARC_LOG_INFO("Opened '{}': {} objects in {:.1f} ms", path, m_scene->objectCount(), timer.elapsedMs());
        return true;
    }

    bool SceneModule::saveScene(const std::string& path)
    {
        if (!writeText(path, writeScene(*m_scene)))
        {
            ARC_LOG_ERROR("Cannot save the scene to '{}'", path);
            return false;
        }
        m_scenePath = path;
        ARC_LOG_INFO("Saved '{}' ({} objects)", path, m_scene->objectCount());
        return true;
    }

    std::string SceneModule::snapshot() const
    {
        return writeScene(*m_scene);
    }

    bool SceneModule::restore(std::string_view snapshot)
    {
        SceneLoadOptions options;
        options.context.resources = &m_assets.resources();
        std::string error;
        const std::vector<std::shared_ptr<const Model>> keepLoaded = loadedModels();
        if (!readScene(*m_scene, snapshot, options, &error))
        {
            ARC_LOG_ERROR("Cannot restore the scene: {}", error);
            return false;
        }
        return true;
    }

    bool SceneModule::savePrefab(const GameObject& root, const std::string& path)
    {
        if (!writeText(path, writePrefab(root)))
        {
            ARC_LOG_ERROR("Cannot save the prefab to '{}'", path);
            return false;
        }
        ARC_LOG_INFO("Saved '{}' as a prefab of '{}'", path, root.name());
        return true;
    }

    GameObject* SceneModule::instantiatePrefab(const std::string& path, GameObject* parent)
    {
        const std::optional<std::string> text = readText(path);
        if (!text)
        {
            ARC_LOG_ERROR("Cannot read the prefab '{}'", path);
            return nullptr;
        }
        SceneLoadOptions options;
        options.context.resources = &m_assets.resources();
        std::string error;
        GameObject* root = arc::instantiatePrefab(*m_scene, *text, parent, options, &error);
        if (!root)
            ARC_LOG_ERROR("Cannot place the prefab '{}': {}", path, error);
        return root;
    }
}

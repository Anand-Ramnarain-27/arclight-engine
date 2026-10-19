#pragma once

#include "Application/Module.h"
#include "Renderer/MeshRenderer.h"
#include "Scene/Scene.h"

#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace arc
{
    class Model;

    /// Owns the scene, updates it, and draws its meshes into the scene target.
    ///
    /// Models are loaded once per path and shared while anything uses them; the cache holds them
    /// weakly, so a model whose last MeshComponent is gone is released. Has no UI, so it may be
    /// added before the editor.
    class SceneModule final : public Module
    {
    public:
        explicit SceneModule(Application& app);
        ~SceneModule() override;

        [[nodiscard]] std::string_view name() const override { return "Scene"; }

        [[nodiscard]] bool init() override;
        void update(const FrameTime& time) override;
        /// Removes the objects destroyed this frame, after every module's update() and before
        /// anything is drawn.
        void preRender() override;
        void render() override;
        void shutdown() override;

        [[nodiscard]] Scene& scene() { return *m_scene; }

        /// The model at `path`, from the cache if it is still loaded. Null, after logging, if it
        /// cannot be imported. Must be called outside render().
        [[nodiscard]] std::shared_ptr<const Model> loadModel(const std::string& path);

        /// Loads the model and instantiates it at the end of `parent`'s children (or the top level).
        /// Returns the new object, or null if the model cannot be loaded.
        GameObject* importModel(const std::string& path, GameObject* parent = nullptr);

        /// Models currently loaded, in no particular order.
        [[nodiscard]] std::vector<std::shared_ptr<const Model>> loadedModels() const;

        MeshRenderer::DebugView debugView = MeshRenderer::DebugView::Lit;

        /// Meshes drawn in the last frame.
        [[nodiscard]] uint32_t drawnMeshCount() const { return m_drawnMeshCount; }

    private:
        std::unique_ptr<Scene> m_scene;
        MeshRenderer m_meshRenderer;
        std::unordered_map<std::string, std::weak_ptr<const Model>> m_models;
        std::vector<MeshDrawItem> m_drawItems;
        uint32_t m_drawnMeshCount = 0;
    };
}

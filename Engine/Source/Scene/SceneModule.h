#pragma once

#include "Application/Module.h"
#include "Renderer/MeshRenderer.h"
#include "Math/Frustum.h"
#include "Renderer/Camera.h"
#include "Renderer/RenderList.h"
#include "Renderer/Skybox.h"
#include "Scene/Scene.h"

#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace arc
{
    class Model;
    class AssetModule;

    /// What frustum culling did in the last frame.
    struct CullingStats
    {
        uint32_t objects = 0;          // Active objects in the scene.
        uint32_t objectsTested = 0;    // Objects whose subtree bounds were tested.
        uint32_t subtreesCulled = 0;   // Whole subtrees skipped after one test.
        uint32_t meshes = 0;           // Visible-if-on-screen meshes in the scene.
        uint32_t meshesTested = 0;     // Meshes tested one by one.
        uint32_t meshesCulled = 0;     // Meshes not drawn, by either test.
    };

    /// Owns the scene, updates it, and draws it into the scene target: every frame the active
    /// meshes the camera can see go into a RenderList, each at a level of detail for its size on
    /// screen, which is sorted and drawn in one scene pass with the skybox between the opaque and
    /// the blended queue.
    ///
    /// Culling is hierarchical: each object's bounds enclose its own meshes and its whole subtree,
    /// so a subtree entirely outside the frustum is skipped with one test, and one entirely inside
    /// is drawn with no further tests. Only objects whose bounds cross the frustum's edge have their
    /// meshes tested one by one, as oriented boxes, which fit rotated meshes more tightly than
    /// world-aligned ones.
    ///
    /// Models come from the ResourceManager, which loads each asset once and shares it while any
    /// MeshComponent uses it. Has no UI, so it may be added before the editor.
    class SceneModule final : public Module
    {
    public:
        /// `assets` must be added to the application before this module.
        SceneModule(Application& app, AssetModule& assets);
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

        /// The model at `path` (virtual or native), shared if it is already loaded. Null, after
        /// logging, if it cannot be imported. Must be called outside render().
        [[nodiscard]] std::shared_ptr<const Model> loadModel(const std::string& path);

        /// Loads the model and instantiates it at the end of `parent`'s children (or the top level).
        /// Returns the new object, or null if the model cannot be loaded.
        GameObject* importModel(const std::string& path, GameObject* parent = nullptr);

        /// Models currently loaded, in no particular order.
        [[nodiscard]] std::vector<std::shared_ptr<const Model>> loadedModels() const;

        // Scene files. Paths are virtual or native. Each must be called outside render(), and
        // replacing the scene makes every GameObject pointer and handle into it stale.

        /// Removes every object; the scene has no file until it is saved.
        void newScene();
        /// Replaces the scene with the file's. On failure the scene is left as it was.
        bool openScene(const std::string& path);
        /// Writes the scene and remembers `path` as its file.
        bool saveScene(const std::string& path);
        /// The file the scene was opened from or last saved to; empty for a new scene.
        [[nodiscard]] const std::string& scenePath() const { return m_scenePath; }

        /// The scene as it would be saved, to compare against a saved copy or restore later.
        [[nodiscard]] std::string snapshot() const;
        /// Replaces the scene with a snapshot(). The scene's file is unchanged.
        bool restore(std::string_view snapshot);

        /// Writes `root` and its subtree to a prefab file.
        bool savePrefab(const GameObject& root, const std::string& path);
        /// A new copy of the prefab at the end of `parent`'s children, or null after logging.
        GameObject* instantiatePrefab(const std::string& path, GameObject* parent = nullptr);

        MeshRenderer::DebugView debugView = MeshRenderer::DebugView::Lit;

        /// Off draws opaque primitives in scene order, to compare the state changes with sorting.
        bool sortDraws = true;

        /// Off draws every mesh, to compare.
        bool frustumCulling = true;
        /// Keeps culling and level-of-detail selection at the camera as it was when this was set, so
        /// the camera can fly out and see what is drawn.
        bool freezeCulling = false;
        /// The coarsest level of detail whose error covers at most this many pixels is drawn.
        float lodErrorPixels = 1.0f;
        /// -1 chooses levels by error; 0 and up draws every primitive at that level, or its coarsest.
        int forcedLod = -1;

        [[nodiscard]] Skybox& skybox() { return m_skybox; }

        /// What the last frame drew.
        [[nodiscard]] const RenderStats& renderStats() const { return m_stats; }
        [[nodiscard]] const CullingStats& cullingStats() const { return m_cullingStats; }

    private:
        struct CullNode
        {
            const GameObject* object = nullptr;
            uint32_t subtreeEnd = 0;     // One past this node's last descendant in m_cullNodes.
            uint32_t subtreeMeshes = 0;  // Drawable meshes of the node and its descendants.
            Aabb bounds;                 // World space: the node's meshes and its whole subtree.
        };

        uint32_t buildCullNode(const GameObject& object);
        void addMeshes(const GameObject& object, const Frustum* frustum, const LodPolicy& lods);
        void cullAndCollect(const Frustum& frustum, const LodPolicy& lods);

        AssetModule& m_assets;
        std::unique_ptr<Scene> m_scene;
        std::string m_scenePath;
        MeshRenderer m_meshRenderer;
        RenderList m_renderList;
        RenderStats m_stats;
        Skybox m_skybox;
        std::vector<CullNode> m_cullNodes;
        CullingStats m_cullingStats;
        Camera m_cullCamera;
        float m_cullAspect = 1.0f;
    };
}

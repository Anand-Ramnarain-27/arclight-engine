#pragma once

#include "Application/Module.h"
#include "Platform/PlatformEvents.h"
#include "Renderer/MeshRenderer.h"

#include <array>
#include <memory>
#include <string>

namespace arc
{
    class Model;

    /// Shows one glTF model in the scene. Opens the sample model (Assets/Models/Lantern.glb) at
    /// start-up; others are opened from the Model window or by dropping a .gltf or .glb file on the
    /// editor window. The Model window also lists what was imported and switches the shading to
    /// debug views of base colour, normals and texture coordinates.
    ///
    /// Placeholder until the scene and its components exist (Phase 3).
    class ModelViewerModule final : public Module, public PlatformEventListener
    {
    public:
        explicit ModelViewerModule(Application& app);
        ~ModelViewerModule() override;

        [[nodiscard]] std::string_view name() const override { return "Model Viewer"; }

        [[nodiscard]] bool init() override;
        void update(const FrameTime& time) override;
        void render() override;
        void shutdown() override;

        void onPlatformEvent(const SDL_Event& event) override;

    private:
        void load(const std::string& path);
        void drawWindow();
        [[nodiscard]] Mat4 modelTransform() const;

        MeshRenderer m_meshRenderer;
        std::unique_ptr<Model> m_model;
        std::string m_pendingPath;
        std::array<char, 1024> m_pathInput{};
        double m_loadMilliseconds = 0.0;

        bool m_visible = true;
        bool m_fitToView = true;
        float m_yawDegrees = 0.0f;
        MeshRenderer::DebugView m_debugView = MeshRenderer::DebugView::Lit;
    };
}

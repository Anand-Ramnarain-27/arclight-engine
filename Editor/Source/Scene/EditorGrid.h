#pragma once

#include <array>
#include <memory>

namespace arc
{
    class Renderer;

    namespace rhi
    {
        class Pipeline;
    }

    /// The infinite ground grid of the Scene view (Engine/Shaders/Grid.hlsl): minor lines every
    /// cellSize, major lines every majorEvery cells, the X axis in red and the Z axis in blue.
    ///
    /// Draws its own scene pass, which must come after every pass that draws opaque geometry: it
    /// blends over what is there and tests against its depth without writing any.
    class EditorGrid
    {
    public:
        EditorGrid();
        ~EditorGrid();

        EditorGrid(const EditorGrid&) = delete;
        EditorGrid& operator=(const EditorGrid&) = delete;

        /// Returns false and logs the reason on failure.
        [[nodiscard]] bool init(Renderer& renderer);
        void shutdown();

        void render(Renderer& renderer);

        bool visible = true;
        float cellSize = 1.0f;         // World units.
        float majorEvery = 10.0f;      // Minor cells per major cell.
        float fadeDistance = 150.0f;   // World units from the camera.
        float opacity = 1.0f;

    private:
        // Indexed by Camera::reversedZ: the depth test differs between the two conventions.
        std::array<std::unique_ptr<rhi::Pipeline>, 2> m_pipelines;
    };
}

#pragma once

#include "Math/Math.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>

namespace arc
{
    class Model;
    class Renderer;

    namespace rhi
    {
        class Pipeline;
        class Sampler;
        class Texture;
    }

    class RenderList;

    /// What one MeshRenderer::draw() recorded. State changes are counted only when the state really
    /// changed; a sorted list needs far fewer than an unsorted one for the same draws.
    struct RenderStats
    {
        uint32_t draws = 0;
        uint32_t triangles = 0;
        uint32_t pipelineChanges = 0;
        uint32_t bufferBinds = 0;          // Vertex and index buffers.
        uint32_t textureBinds = 0;         // Texture and sampler pairs.
        uint32_t constantUploads = 0;      // Per-object and per-material cbuffers.
    };

    /// Draws Models with Engine/Shaders/Mesh.hlsl into the scene.
    ///
    /// Owns the pipelines (one per combination of depth convention, culling, blending and winding)
    /// and the stand-in textures: white for materials without a base colour texture, so the factor
    /// alone decides the colour, and a magenta checker for textures whose image is missing or could
    /// not be decoded, so the gap is obvious instead of silently white.
    class MeshRenderer
    {
    public:
        /// What the pixel shader outputs. Values match DEBUG_VIEW_ in Mesh.hlsl.
        enum class DebugView : uint32_t
        {
            Lit = 0,
            BaseColor = 1,
            Normals = 2,      // World-space normal, mapped from [-1, 1] to [0, 1].
            TexCoords = 3,    // TEXCOORD_0's fractional part in red and green.
        };

        MeshRenderer();
        ~MeshRenderer();

        MeshRenderer(const MeshRenderer&) = delete;
        MeshRenderer& operator=(const MeshRenderer&) = delete;

        /// Returns false and logs the reason on failure.
        [[nodiscard]] bool init(Renderer& renderer);
        void shutdown();

        /// Records the opaque queue, then the blended one, each in its debug group. Must be called
        /// inside a scene pass, with the list already sorted.
        RenderStats draw(Renderer& renderer, const RenderList& list, DebugView debugView);

    private:
        struct PipelineKey
        {
            bool reversedZ = true;
            bool doubleSided = false;
            bool blend = false;
            /// The instance's transform mirrors it (negative determinant), which turns its
            /// counter-clockwise triangles clockwise on screen.
            bool mirrored = false;

            [[nodiscard]] size_t index() const
            {
                return size_t{reversedZ} | size_t{doubleSided} << 1 | size_t{blend} << 2 | size_t{mirrored} << 3;
            }
        };

        std::array<std::unique_ptr<rhi::Pipeline>, 16> m_pipelines;   // By PipelineKey::index().
        std::unique_ptr<rhi::Texture> m_whiteTexture;
        std::unique_ptr<rhi::Texture> m_missingTexture;
        std::unique_ptr<rhi::Sampler> m_defaultSampler;
    };
}

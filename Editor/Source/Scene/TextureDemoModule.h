#pragma once

#include "Application/Module.h"
#include "RHI/RHITypes.h"

#include <array>
#include <memory>
#include <string>
#include <vector>

namespace arc
{
    namespace rhi
    {
        class Buffer;
        class Pipeline;
        class Sampler;
        class Texture;
    }

    /// Shows how textures are sampled and how depth precision runs out. A 40 x 40 ground plane
    /// around the origin, seen through the editor camera, is large enough to use every mip level,
    /// and a quad in the corner has texture coordinates from -1 to 2 so the address mode decides
    /// what lies outside [0, 1]. The z-fighting test puts two walls a small distance apart far down
    /// -Z; with too little depth precision they flicker through each other. The Texture Demo window
    /// holds every setting.
    ///
    /// Every shape is one unit quad drawn with different per-draw constants (b1).
    class TextureDemoModule final : public Module
    {
    public:
        explicit TextureDemoModule(Application& app);
        ~TextureDemoModule() override;

        [[nodiscard]] std::string_view name() const override { return "Texture Demo"; }

        [[nodiscard]] bool init() override;
        void update(const FrameTime& time) override;
        void render() override;
        void shutdown() override;

    private:
        struct DemoTexture
        {
            std::string name;
            std::unique_ptr<rhi::Texture> texture;
            bool colorPerMipLevel = false;
        };

        [[nodiscard]] bool createTextures();
        [[nodiscard]] bool rebuildSamplers();
        void drawWindow();

        // Indexed by Camera::reversedZ, whose depth test differs. The overlay pipeline draws the
        // corner quad, which is in clip space and must not be depth-tested against the scene.
        std::array<std::unique_ptr<rhi::Pipeline>, 2> m_pipelines;
        std::unique_ptr<rhi::Pipeline> m_overlayPipeline;
        std::unique_ptr<rhi::Buffer> m_vertexBuffer;
        std::unique_ptr<rhi::Buffer> m_indexBuffer;

        std::vector<DemoTexture> m_textures;
        size_t m_selectedTexture = 0;

        rhi::SamplerDesc m_groundSamplerDesc;
        rhi::SamplerDesc m_quadSamplerDesc;
        std::unique_ptr<rhi::Sampler> m_groundSampler;
        std::unique_ptr<rhi::Sampler> m_quadSampler;
        bool m_samplersDirty = true;
        bool m_recreateTextures = false;

        bool m_visible = true;
        float m_mipBias = 0.0f;

        std::unique_ptr<rhi::Texture> m_nearWallTexture;
        std::unique_ptr<rhi::Texture> m_farWallTexture;
        bool m_zFightingTest = false;
        float m_wallDistance = 200.0f;     // World units down -Z from the origin.
        float m_wallSeparation = 0.02f;    // World units between the two walls.
    };
}

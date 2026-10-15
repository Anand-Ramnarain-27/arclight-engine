#pragma once

#include "Application/Module.h"
#include "RHI/RHITypes.h"

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

    /// Shows how textures are sampled. A ground plane recedes far enough into the distance to use
    /// every mip level, and a quad in the corner has texture coordinates from -1 to 2 so the address
    /// mode decides what lies outside [0, 1]. The Texture Demo window picks the texture and the
    /// sampler settings.
    ///
    /// Both shapes are one unit quad drawn twice with different per-draw constants (b1); the ground
    /// also takes a camera in the view constants (b0). Placeholder until the scene viewport and the
    /// editor camera exist (Phase 2).
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

        std::unique_ptr<rhi::Pipeline> m_pipeline;
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

        float m_mipBias = 0.0f;
        float m_fieldOfView = 60.0f;   // Degrees, vertical.
        float m_cameraHeight = 1.2f;   // World units above the ground.
    };
}

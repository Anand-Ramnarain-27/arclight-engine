#include "Renderer/EnvironmentMaps.h"

#include "Core/Log.h"
#include "RHI/CommandList.h"
#include "RHI/Device.h"
#include "RHI/Pipeline.h"
#include "RHI/Sampler.h"
#include "RHI/Shader.h"
#include "RHI/Texture.h"
#include "Renderer/ShaderLibrary.h"

#include <algorithm>
#include <bit>
#include <cmath>
#include <string>

namespace arc
{
    namespace
    {
        // Mirrors IblConstants in Ibl.hlsli: five scalars, padded to two 16-byte rows.
        struct IblConstants
        {
            uint32_t face = 0;
            float sourceLod = 0.0f;
            float roughness = 0.0f;
            float sourceSize = 0.0f;
            uint32_t sampleCount = 0;
            uint32_t padding[3]{};
        };
        static_assert(sizeof(IblConstants) == 32);

        // Enough for smooth results once filtered importance sampling reads blurred mips; the BRDF
        // table has no mips to lean on, but is only made once.
        constexpr uint32_t kPrefilterSamples = 512;
        constexpr uint32_t kBrdfLutSamples = 1024;

        constexpr const char* kShaderNames[] = {"IblFromEquirect", "IblFromCube", "IblIrradiance", "IblPrefilter",
                                                "IblBrdfLut"};

        constexpr rhi::Format kFormat = rhi::Format::RGBA16_FLOAT;

        std::unique_ptr<rhi::Texture> createTarget(rhi::Device& device, uint32_t size, uint32_t mipLevels,
                                                   rhi::TextureType type, std::string debugName)
        {
            return device.createTexture({
                .size = {size, size},
                .format = kFormat,
                .mipLevels = mipLevels,
                .usage = rhi::TextureUsage::Sampled | rhi::TextureUsage::RenderTarget,
                .type = type,
                .debugName = std::move(debugName),
            });
        }

        // Draws every face of every mip level of `target` with the pipeline that is set, once per
        // subresource; `constantsFor` fills the constants of one (face, mip).
        template <typename ConstantsFor>
        void drawCubeFaces(rhi::CommandList& commandList, rhi::Pipeline& pipeline, rhi::Texture& target,
                           rhi::Texture& source, rhi::Sampler& sampler, ConstantsFor constantsFor)
        {
            const rhi::TextureDesc& desc = target.desc();
            for (uint32_t mip = 0; mip < desc.mipLevels; ++mip)
                for (uint32_t face = 0; face < 6; ++face)
                {
                    commandList.beginRenderPass({
                        .color = {.texture = &target, .mipLevel = mip, .arrayLayer = face, .load = rhi::LoadOp::DontCare},
                        .debugName = desc.debugName,
                    });
                    commandList.setPipeline(pipeline);
                    commandList.setConstants(1, constantsFor(face, mip));
                    commandList.setTexture(0, source);
                    commandList.setSampler(0, sampler);
                    commandList.draw(3);
                    commandList.endRenderPass();
                }
            commandList.barrier(target, rhi::ResourceState::ShaderRead);
        }

        float mipLevelOf(float sourceTexels, uint32_t targetTexels, uint32_t sourceMipLevels)
        {
            const float lod = std::log2(sourceTexels / static_cast<float>(targetTexels));
            return std::clamp(lod, 0.0f, static_cast<float>(sourceMipLevels - 1));
        }
    }

    EnvironmentMaps::EnvironmentMaps() = default;

    // Defined here, where the RHI types are complete, for the unique_ptr destructors.
    EnvironmentMaps::~EnvironmentMaps() = default;

    uint32_t EnvironmentMaps::radianceSizeForEquirect(uint32_t width)
    {
        // A face spans a quarter of the image's width (90 of 360 degrees).
        const uint32_t quarter = std::max(width / 4, 1u);
        return std::clamp(std::bit_floor(quarter), 16u, kMaxRadianceSize);
    }

    bool EnvironmentMaps::init(rhi::Device& device, const ShaderLibrary& shaders)
    {
        m_device = &device;
        for (uint32_t pass = 0; pass < kPassCount; ++pass)
        {
            const auto vertexShader = shaders.load(kShaderNames[pass], rhi::ShaderStage::Vertex, "VSMain");
            const auto pixelShader = shaders.load(kShaderNames[pass], rhi::ShaderStage::Pixel, "PSMain");
            if (!vertexShader || !pixelShader)
                return false;
            rhi::PipelineDesc desc;
            desc.vertexShader = vertexShader.get();
            desc.pixelShader = pixelShader.get();
            desc.colorFormat = kFormat;
            desc.cullMode = rhi::CullMode::None;
            desc.debugName = kShaderNames[pass];
            m_pipelines[pass] = device.createPipeline(desc);
            if (!m_pipelines[pass])
                return false;
        }

        // Equirectangular images wrap around left to right but not over the poles.
        m_equirectSampler = device.createSampler({
            .addressU = rhi::AddressMode::Repeat,
            .addressV = rhi::AddressMode::ClampToEdge,
            .addressW = rhi::AddressMode::ClampToEdge,
            .debugName = "Equirect Sampler",
        });
        m_cubeSampler = device.createSampler({
            .addressU = rhi::AddressMode::ClampToEdge,
            .addressV = rhi::AddressMode::ClampToEdge,
            .addressW = rhi::AddressMode::ClampToEdge,
            .debugName = "Environment Sampler",
        });
        if (!m_equirectSampler || !m_cubeSampler)
            return false;

        m_brdfLut = createTarget(device, kBrdfLutSize, 1, rhi::TextureType::Texture2D, "BRDF Table");
        if (!m_brdfLut)
            return false;
        device.submitImmediate([&](rhi::CommandList& commandList) {
            const rhi::ScopedDebugGroup group(commandList, "BRDF Table");
            commandList.beginRenderPass({
                .color = {.texture = m_brdfLut.get(), .load = rhi::LoadOp::DontCare},
                .debugName = "BRDF Table",
            });
            commandList.setPipeline(*m_pipelines[kBrdfLut]);
            commandList.setConstants(1, IblConstants{.sampleCount = kBrdfLutSamples});
            commandList.draw(3);
            commandList.endRenderPass();
            commandList.barrier(*m_brdfLut, rhi::ResourceState::ShaderRead);
        });
        return true;
    }

    void EnvironmentMaps::shutdown()
    {
        for (auto& pipeline : m_pipelines)
            pipeline.reset();
        m_equirectSampler.reset();
        m_cubeSampler.reset();
        m_radiance.reset();
        m_irradiance.reset();
        m_prefiltered.reset();
        m_brdfLut.reset();
        m_device = nullptr;
    }

    bool EnvironmentMaps::bakeFromEquirect(rhi::Texture& equirect, std::string_view name)
    {
        const rhi::TextureDesc& desc = equirect.desc();
        const uint32_t size = radianceSizeForEquirect(desc.size.width);
        return bake(equirect, kFromEquirect, size, static_cast<float>(desc.size.width) / 4.0f, name);
    }

    bool EnvironmentMaps::bakeFromCube(rhi::Texture& cube, std::string_view name)
    {
        const uint32_t sourceSize = cube.desc().size.width;
        const uint32_t size = std::clamp(std::bit_floor(sourceSize), 16u, kMaxRadianceSize);
        return bake(cube, kFromCube, size, static_cast<float>(sourceSize), name);
    }

    bool EnvironmentMaps::bake(rhi::Texture& source, Pass pass, uint32_t radianceSize, float sourceTexelsPerFace,
                               std::string_view name)
    {
        if (!m_device || !m_pipelines[pass])
        {
            ARC_LOG_ERROR("Environment '{}': EnvironmentMaps is not initialised", name);
            return false;
        }
        const rhi::TextureDesc& sourceDesc = source.desc();
        const bool sourceIsCube = sourceDesc.type == rhi::TextureType::Cube;
        if (sourceIsCube != (pass == kFromCube) || !hasFlag(sourceDesc.usage, rhi::TextureUsage::Sampled) ||
            source.state() != rhi::ResourceState::ShaderRead)
        {
            ARC_LOG_ERROR("Environment '{}': '{}' must be a sampled {} texture in the ShaderRead state", name,
                          sourceDesc.debugName, pass == kFromCube ? "cube" : "2D");
            return false;
        }

        const std::string label(name);
        auto radiance = createTarget(*m_device, radianceSize, rhi::fullMipCount({radianceSize, radianceSize}),
                                     rhi::TextureType::Cube, "Environment Radiance (" + label + ")");
        auto irradiance = createTarget(*m_device, kIrradianceSize, 1, rhi::TextureType::Cube,
                                       "Environment Irradiance (" + label + ")");
        auto prefiltered = createTarget(*m_device, kPrefilteredSize, kPrefilteredLevels, rhi::TextureType::Cube,
                                        "Environment Prefiltered (" + label + ")");
        if (!radiance || !irradiance || !prefiltered)
            return false;

        rhi::Sampler& sourceSampler = pass == kFromEquirect ? *m_equirectSampler : *m_cubeSampler;
        m_device->submitImmediate([&](rhi::CommandList& commandList) {
            const rhi::ScopedDebugGroup group(commandList, "Bake Environment");

            // Each radiance mip reads the source at its own resolution, so small mips are averages of
            // the source rather than aliased point samples of it.
            drawCubeFaces(commandList, *m_pipelines[pass], *radiance, source, sourceSampler,
                          [&](uint32_t face, uint32_t mip) {
                              const uint32_t faceSize = rhi::mipSize({radianceSize, radianceSize}, mip).width;
                              return IblConstants{
                                  .face = face,
                                  .sourceLod = mipLevelOf(sourceTexelsPerFace, faceSize, sourceDesc.mipLevels),
                              };
                          });

            const float radianceTexels = static_cast<float>(radianceSize);
            drawCubeFaces(commandList, *m_pipelines[kIrradiance], *irradiance, *radiance, *m_cubeSampler,
                          [&](uint32_t face, uint32_t) {
                              return IblConstants{.face = face, .sourceSize = radianceTexels};
                          });

            const uint32_t radianceMips = radiance->desc().mipLevels;
            drawCubeFaces(commandList, *m_pipelines[kPrefilter], *prefiltered, *radiance, *m_cubeSampler,
                          [&](uint32_t face, uint32_t mip) {
                              const uint32_t faceSize = rhi::mipSize({kPrefilteredSize, kPrefilteredSize}, mip).width;
                              return IblConstants{
                                  .face = face,
                                  .sourceLod = mipLevelOf(radianceTexels, faceSize, radianceMips),
                                  .roughness = static_cast<float>(mip) / prefilteredMaxLod(),
                                  .sourceSize = radianceTexels,
                                  .sampleCount = kPrefilterSamples,
                              };
                          });
        });

        m_radiance = std::move(radiance);
        m_irradiance = std::move(irradiance);
        m_prefiltered = std::move(prefiltered);
        ARC_LOG_INFO("Baked image-based lighting for '{}' ({} px radiance)", name, radianceSize);
        return true;
    }
}

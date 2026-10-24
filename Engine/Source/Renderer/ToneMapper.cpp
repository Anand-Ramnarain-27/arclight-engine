#include "Renderer/ToneMapper.h"

#include "RHI/CommandList.h"
#include "RHI/Device.h"
#include "RHI/Pipeline.h"
#include "RHI/Shader.h"
#include "RHI/Texture.h"
#include "Renderer/ShaderLibrary.h"

#include <cmath>
#include <cstddef>

namespace arc
{
    namespace
    {
        // Mirrors ToneMapConstants in ToneMap.hlsl: three scalars, padded to one 16-byte row.
        struct ToneMapConstants
        {
            float exposureScale = 1.0f;
            uint32_t toneOperator = 0;
            uint32_t passThrough = 0;
            uint32_t padding = 0;
        };
        static_assert(offsetof(ToneMapConstants, passThrough) == 8);
        static_assert(sizeof(ToneMapConstants) == 16);
    }

    const char* toneMapOperatorName(ToneMapOperator op)
    {
        switch (op)
        {
            case ToneMapOperator::Clamp:             return "None (clamp)";
            case ToneMapOperator::Reinhard:          return "Reinhard";
            case ToneMapOperator::Aces:              return "ACES (Hill fit)";
            case ToneMapOperator::KhronosPbrNeutral: return "Khronos PBR Neutral";
        }
        return "";
    }

    ToneMapper::ToneMapper() = default;

    // Defined here, where Pipeline is complete, for the unique_ptr destructor.
    ToneMapper::~ToneMapper() = default;

    bool ToneMapper::init(rhi::Device& device, const ShaderLibrary& shaders, rhi::Format targetFormat)
    {
        const auto vertexShader = shaders.load("ToneMap", rhi::ShaderStage::Vertex, "VSMain");
        const auto pixelShader = shaders.load("ToneMap", rhi::ShaderStage::Pixel, "PSMain");
        if (!vertexShader || !pixelShader)
            return false;

        rhi::PipelineDesc desc;
        desc.vertexShader = vertexShader.get();
        desc.pixelShader = pixelShader.get();
        desc.colorFormat = targetFormat;
        desc.cullMode = rhi::CullMode::None;
        desc.debugName = "Tone Map";
        m_pipeline = device.createPipeline(desc);
        return m_pipeline != nullptr;
    }

    void ToneMapper::shutdown()
    {
        m_pipeline.reset();
    }

    void ToneMapper::draw(rhi::CommandList& commandList, rhi::Texture& scene) const
    {
        if (!m_pipeline)
            return;
        commandList.setPipeline(*m_pipeline);
        commandList.setTexture(0, scene);
        commandList.setConstants(1, ToneMapConstants{
                                        .exposureScale = std::exp2(exposure),
                                        .toneOperator = static_cast<uint32_t>(op),
                                        .passThrough = passThrough ? 1u : 0u,
                                    });
        commandList.draw(3);
    }
}

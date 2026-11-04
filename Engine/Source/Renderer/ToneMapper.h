#pragma once

#include "RHI/RHITypes.h"

#include <cstdint>
#include <memory>

namespace arc
{
    class ShaderLibrary;

    namespace rhi
    {
        class CommandList;
        class Device;
        class Pipeline;
    }

    /// The curve that maps scene brightness, from 0 to unbounded, onto the display's 0 to 1. Values
    /// match TONE_ in Engine/Shaders/ToneMap.hlsl.
    enum class ToneMapOperator : uint32_t
    {
        /// No curve: everything above 1 clips to white.
        Clamp = 0,
        /// x / (1 + x) per channel.
        Reinhard = 1,
        /// Stephen Hill's fit of the ACES filmic transform: contrasty, shifts bright colours toward white.
        Aces = 2,
        /// Khronos PBR Neutral: leaves colours below about 0.76 as they are, so base colours stay true.
        KhronosPbrNeutral = 3,
    };

    [[nodiscard]] const char* toneMapOperatorName(ToneMapOperator op);

    /// Draws the HDR scene into an LDR target: exposure, a tone curve, and sRGB encoding, as one
    /// full-screen triangle (Engine/Shaders/ToneMap.hlsl). Owned by the Renderer.
    class ToneMapper
    {
    public:
        ToneMapper();
        ~ToneMapper();

        ToneMapper(const ToneMapper&) = delete;
        ToneMapper& operator=(const ToneMapper&) = delete;

        /// `targetFormat` is the format of the target draw() writes to. Returns false and logs on failure.
        [[nodiscard]] bool init(rhi::Device& device, const ShaderLibrary& shaders, rhi::Format targetFormat);
        void shutdown();

        /// Must be called inside a render pass on a target of the format given to init(), with no
        /// depth target, and the same size as `scene`, which must be in the ShaderRead state.
        void draw(rhi::CommandList& commandList, rhi::Texture& scene) const;

        ToneMapOperator op = ToneMapOperator::KhronosPbrNeutral;
        /// Stops: each +1 doubles the brightness before the curve.
        float exposure = 0.0f;
        /// Shows the scene's values as they are, encoded but without exposure or curve. For debug
        /// views whose output is data (normals, texture coordinates), not light.
        bool passThrough = false;

    private:
        std::unique_ptr<rhi::Pipeline> m_pipeline;
    };
}

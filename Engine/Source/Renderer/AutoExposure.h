#pragma once

#include "Core/Time.h"
#include "RHI/RHITypes.h"

#include <array>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>

namespace arc
{
    class ShaderLibrary;

    namespace rhi
    {
        class Buffer;
        class CommandList;
        class ComputePipeline;
        class Device;
        class Texture;
    }

    /// Exposure that follows the scene's brightness, measured on the GPU with two compute passes
    /// (Engine/Shaders/LuminanceHistogram.hlsl and ExposureAverage.hlsl):
    ///   1. a histogram of the scene's pixels by log2 luminance, counted per thread group in
    ///      group-shared memory and added to a storage buffer;
    ///   2. one group that reduces the histogram to the average log luminance (wave intrinsics, then a
    ///      group-shared tree) and moves the adapted luminance toward it over time.
    /// The tone-mapping pass reads the adapted luminance from the state buffer, so the exposure never
    /// goes through the CPU. The CPU gets a copy of the histogram kMaxFramesInFlight frames later, for
    /// the editor. Owned by the Renderer.
    class AutoExposure
    {
    public:
        /// Match HISTOGRAM_BIN_COUNT and the state layout in Engine/Shaders/Exposure.hlsli.
        static constexpr uint32_t kBinCount = 256;
        static constexpr uint32_t kStateCount = kBinCount + 2;

        AutoExposure();
        ~AutoExposure();

        AutoExposure(const AutoExposure&) = delete;
        AutoExposure& operator=(const AutoExposure&) = delete;

        /// Returns false and logs on failure.
        [[nodiscard]] bool init(rhi::Device& device, const ShaderLibrary& shaders);
        void shutdown();

        /// Records the measurement if `enabled`, and leaves the state buffer in the ShaderRead state
        /// for the tone-mapping pass either way. Outside any pass; `scene` must be in the ShaderRead
        /// state. `frameSlot` picks the readback buffer that collectReadback() reads when the slot comes
        /// round again.
        void record(rhi::CommandList& commandList, rhi::Texture& scene, uint32_t frameSlot);

        /// Copies what the GPU measured in the frame that last used `frameSlot`. The GPU must have
        /// finished that frame (Device::beginFrame() has waited for it).
        void collectReadback(uint32_t frameSlot);

        /// The state buffer the tone-mapping pass reads at t4: a StructuredBuffer<uint>.
        [[nodiscard]] rhi::Buffer& stateBuffer() { return *m_state; }

        /// The newest histogram the CPU has: pixel counts per bin, bin 0 for black pixels.
        [[nodiscard]] std::span<const uint32_t> histogram() const { return m_histogram; }
        /// Luminance, in the scene target's units before exposure, at the bottom of bin 1 and the top
        /// of the last bin.
        [[nodiscard]] float minLuminance() const;
        [[nodiscard]] float maxLuminance() const;
        /// The newest frame's average and the adapted value the exposure follows; 0 until measured.
        [[nodiscard]] float frameLuminance() const { return m_frameLuminance; }
        [[nodiscard]] float adaptedLuminance() const { return m_adaptedLuminance; }

        /// Measure and adapt. Off: the exposure is only ToneMapper::exposure.
        bool enabled = false;
        /// What the adapted luminance maps to before the tone curve: 0.18 puts the scene's average at
        /// middle grey. ToneMapper::exposure still applies on top, as compensation.
        float keyValue = 0.18f;
        /// Limits on the automatic part of the exposure, in stops.
        float minEv = -10.0f;
        float maxEv = 10.0f;
        /// Adaptation speeds, per second: larger is faster. darkenSpeed applies when the scene gets
        /// brighter and the exposure falls; eyes adapt to light faster than to dark.
        float darkenSpeed = 3.0f;
        float brightenSpeed = 1.0f;
        /// log2 luminance range the histogram covers; values outside land in the first or last bin.
        float minLogLuminance = -12.0f;
        float maxLogLuminance = 16.0f;

        /// Jumps straight to the next measured average instead of adapting, as on a camera cut.
        void resetAdaptation() { m_reset = true; }

    private:
        std::unique_ptr<rhi::ComputePipeline> m_histogramPipeline;
        std::unique_ptr<rhi::ComputePipeline> m_averagePipeline;
        std::unique_ptr<rhi::Buffer> m_histogramBuffer;
        std::unique_ptr<rhi::Buffer> m_state;
        std::array<std::unique_ptr<rhi::Buffer>, rhi::kMaxFramesInFlight> m_readback;
        std::array<bool, rhi::kMaxFramesInFlight> m_readbackPending{};

        std::array<uint32_t, kBinCount> m_histogram{};
        float m_frameLuminance = 0.0f;
        float m_adaptedLuminance = 0.0f;

        std::optional<Clock::time_point> m_lastMeasurement;
        bool m_reset = true;
    };
}

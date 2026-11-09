#include "Renderer/AutoExposure.h"

#include "Core/Assert.h"
#include "Core/Log.h"
#include "Core/Profiler.h"
#include "RHI/Buffer.h"
#include "RHI/BufferUtils.h"
#include "RHI/CommandList.h"
#include "RHI/Device.h"
#include "RHI/Pipeline.h"
#include "RHI/Shader.h"
#include "RHI/Texture.h"
#include "Renderer/ShaderLibrary.h"

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstring>
#include <format>
#include <vector>

namespace arc
{
    namespace
    {
        // Must match [numthreads] in LuminanceHistogram.hlsl. 16 x 16 = kBinCount, one thread per bin
        // when the group clears and merges its counts.
        constexpr rhi::ThreadGroupSize kHistogramGroup{16, 16, 1};
        static_assert(kHistogramGroup.count() == AutoExposure::kBinCount);

        // Mirrors HistogramConstants in LuminanceHistogram.hlsl.
        struct HistogramConstants
        {
            uint32_t sceneWidth = 0;
            uint32_t sceneHeight = 0;
            float minLogLuminance = 0.0f;
            float inverseLogLuminanceRange = 0.0f;
        };
        static_assert(sizeof(HistogramConstants) == 16);

        // Mirrors ExposureConstants in ExposureAverage.hlsl.
        struct ExposureConstants
        {
            uint32_t pixelCount = 0;
            float minLogLuminance = 0.0f;
            float logLuminanceRange = 0.0f;
            float adaptBrighter = 0.0f;
            float adaptDarker = 0.0f;
            uint32_t resetAdaptation = 0;
            uint32_t padding[2]{};
        };
        static_assert(offsetof(ExposureConstants, adaptBrighter) == 12);
        static_assert(sizeof(ExposureConstants) == 32);

        constexpr uint64_t kStateBytes = AutoExposure::kStateCount * sizeof(uint32_t);

        std::unique_ptr<rhi::ComputePipeline> createPipeline(rhi::Device& device, const ShaderLibrary& shaders,
                                                             const char* name, rhi::ThreadGroupSize group)
        {
            const auto shader = shaders.load(name, rhi::ShaderStage::Compute, "CSMain");
            if (!shader)
                return nullptr;
            return device.createComputePipeline({.computeShader = shader.get(), .threadGroupSize = group, .debugName = name});
        }

        // Fraction of the remaining distance covered in `seconds` by an exponential approach at `speed`
        // per second, so the adaptation looks the same at any frame rate.
        float adaptationStep(float speed, float seconds)
        {
            return 1.0f - std::exp(-std::max(speed, 0.0f) * seconds);
        }
    }

    AutoExposure::AutoExposure() = default;

    // Defined here, where the RHI types are complete, for the unique_ptr destructors.
    AutoExposure::~AutoExposure() = default;

    bool AutoExposure::init(rhi::Device& device, const ShaderLibrary& shaders)
    {
        m_histogramPipeline = createPipeline(device, shaders, "LuminanceHistogram", kHistogramGroup);
        m_averagePipeline = createPipeline(device, shaders, "ExposureAverage", {kBinCount, 1, 1});
        if (!m_histogramPipeline || !m_averagePipeline)
            return false;

        m_histogramBuffer = device.createBuffer({
            .size = kBinCount * sizeof(uint32_t),
            .usage = rhi::BufferUsage::Storage,
            .stride = sizeof(uint32_t),
            .debugName = "Luminance Histogram",
        });
        // Structured too: the tone-mapping pass reads it as a StructuredBuffer.
        m_state = device.createBuffer({
            .size = kStateBytes,
            .usage = rhi::BufferUsage::Storage | rhi::BufferUsage::Structured,
            .stride = sizeof(uint32_t),
            .debugName = "Exposure State",
        });
        if (!m_histogramBuffer || !m_state)
            return false;
        for (uint32_t slot = 0; slot < rhi::kMaxFramesInFlight; ++slot)
        {
            m_readback[slot] = device.createBuffer({
                .size = kStateBytes,
                .memory = rhi::MemoryType::Readback,
                .debugName = std::format("Exposure Readback {}", slot),
            });
            if (!m_readback[slot])
                return false;
        }

        // GPU-only memory starts undefined; the histogram pass adds to whatever is there.
        const std::vector<std::byte> zeros(kStateBytes);
        return rhi::uploadBuffer(device, *m_histogramBuffer, std::span(zeros).first(m_histogramBuffer->desc().size)) &&
               rhi::uploadBuffer(device, *m_state, zeros);
    }

    void AutoExposure::shutdown()
    {
        m_histogramPipeline.reset();
        m_averagePipeline.reset();
        m_histogramBuffer.reset();
        m_state.reset();
        for (auto& readback : m_readback)
            readback.reset();
    }

    float AutoExposure::minLuminance() const
    {
        return std::exp2(minLogLuminance);
    }

    float AutoExposure::maxLuminance() const
    {
        return std::exp2(maxLogLuminance);
    }

    void AutoExposure::record(rhi::CommandList& commandList, rhi::Texture& scene, uint32_t frameSlot)
    {
        if (!m_state)
            return;
        if (!enabled)
        {
            // When it is turned on again, the exposure starts from the scene as it is then.
            m_reset = true;
            m_lastMeasurement.reset();
            commandList.barrier(*m_state, rhi::ResourceState::ShaderRead);
            return;
        }

        ARC_PROFILE_SCOPE("Auto exposure");
        const Clock::time_point now = Clock::now();
        // A long gap (a breakpoint, a minimised window) would otherwise finish the adaptation at once.
        const float seconds =
            m_lastMeasurement ? std::min(static_cast<float>(Seconds(now - *m_lastMeasurement).count()), 0.25f) : 0.0f;
        m_lastMeasurement = now;

        const Extent2D size = scene.desc().size;
        const float logRange = std::max(maxLogLuminance - minLogLuminance, 1.0f);

        commandList.barrier(*m_histogramBuffer, rhi::ResourceState::UnorderedAccess);
        commandList.barrier(*m_state, rhi::ResourceState::UnorderedAccess);

        commandList.beginComputePass({.debugName = "Auto Exposure"});
        commandList.setComputePipeline(*m_histogramPipeline);
        commandList.setTexture(0, scene);
        commandList.setStorageBuffer(0, *m_histogramBuffer);
        commandList.setConstants(0, HistogramConstants{
                                        .sceneWidth = size.width,
                                        .sceneHeight = size.height,
                                        .minLogLuminance = minLogLuminance,
                                        .inverseLogLuminanceRange = 1.0f / logRange,
                                    });
        commandList.dispatch(rhi::groupCount(size.width, kHistogramGroup.x), rhi::groupCount(size.height, kHistogramGroup.y));

        // The average reads the counts the histogram dispatch has just added.
        commandList.storageBarrier();

        commandList.setComputePipeline(*m_averagePipeline);
        commandList.setStorageBuffer(0, *m_histogramBuffer);
        commandList.setStorageBuffer(1, *m_state);
        commandList.setConstants(0, ExposureConstants{
                                        .pixelCount = size.width * size.height,
                                        .minLogLuminance = minLogLuminance,
                                        .logLuminanceRange = logRange,
                                        .adaptBrighter = adaptationStep(darkenSpeed, seconds),
                                        .adaptDarker = adaptationStep(brightenSpeed, seconds),
                                        .resetAdaptation = m_reset ? 1u : 0u,
                                    });
        commandList.dispatch(1);
        commandList.endComputePass();
        m_reset = false;

        commandList.barrier(*m_state, rhi::ResourceState::CopySource);
        commandList.copyBuffer(*m_readback[frameSlot], 0, *m_state, 0, kStateBytes);
        commandList.barrier(*m_state, rhi::ResourceState::ShaderRead);
        m_readbackPending[frameSlot] = true;
    }

    void AutoExposure::collectReadback(uint32_t frameSlot)
    {
        if (!m_readbackPending[frameSlot])
            return;
        m_readbackPending[frameSlot] = false;

        std::array<uint32_t, kStateCount> state{};
        std::memcpy(state.data(), m_readback[frameSlot]->mappedData(), kStateBytes);
        std::copy_n(state.begin(), kBinCount, m_histogram.begin());
        m_adaptedLuminance = std::bit_cast<float>(state[kBinCount]);
        m_frameLuminance = std::bit_cast<float>(state[kBinCount + 1]);
    }
}

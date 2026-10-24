#pragma once

#include "RHI/RHITypes.h"
#include "Renderer/Lighting.h"

#include <array>
#include <memory>
#include <span>

namespace arc
{
    namespace rhi
    {
        class Device;
    }

    /// The light list the shaders read as StructuredBuffer<Light>: one CPU-writable buffer of
    /// kMaxLights per frame in flight, so the CPU fills this frame's while the GPU may still read the
    /// previous frame's. Lights change every frame (the editor moves them), and a list this small is
    /// read straight from upload memory rather than copied to GPU-only memory first.
    class LightBuffer
    {
    public:
        LightBuffer();
        ~LightBuffer();

        LightBuffer(const LightBuffer&) = delete;
        LightBuffer& operator=(const LightBuffer&) = delete;

        /// Returns false and logs on failure.
        [[nodiscard]] bool init(rhi::Device& device);
        void shutdown();

        /// Writes the first kMaxLights lights into the buffer of `frameSlot`, which the GPU must have
        /// finished with (true between Device::beginFrame() and endFrame() for the current slot).
        /// Returns how many were written.
        uint32_t write(uint32_t frameSlot, std::span<const GpuLight> lights);

        /// Null before init().
        [[nodiscard]] rhi::Buffer* buffer(uint32_t frameSlot) const { return m_buffers[frameSlot].get(); }

    private:
        std::array<std::unique_ptr<rhi::Buffer>, rhi::kMaxFramesInFlight> m_buffers;
    };
}

#pragma once

#include "RHI/RHITypes.h"

#include <functional>
#include <memory>

namespace arc
{
    class Window;
}

namespace arc::rhi
{
    class Buffer;
    class CommandList;
    class Pipeline;
    class Shader;
    struct PipelineDesc;
    struct ShaderDesc;
    class Swapchain;

    /// The GPU, its command queue and the per-frame synchronisation.
    ///
    /// Each build has exactly one real backend: D3D12 on Windows, Metal on macOS. The Null
    /// backend does no GPU work and exists so the engine builds and runs headless elsewhere.
    ///
    /// Frame flow:
    ///   CommandList& cmd = device.beginFrame(swapchain);  // Waits until a frame slot is free.
    ///   ...record...
    ///   device.endFrame(swapchain);                       // Submits and presents.
    class Device
    {
    public:
        /// Returns nullptr and logs the reason if no suitable GPU is found.
        [[nodiscard]] static std::unique_ptr<Device> create(const DeviceDesc& desc);

        virtual ~Device() = default;

        Device(const Device&) = delete;
        Device& operator=(const Device&) = delete;

        [[nodiscard]] virtual const AdapterInfo& adapterInfo() const = 0;

        /// Upload and Readback buffers are mapped on creation. GPU-only buffers start in the Common
        /// state. Returns nullptr and logs on failure.
        [[nodiscard]] virtual std::unique_ptr<Buffer> createBuffer(const BufferDesc& desc) = 0;

        [[nodiscard]] virtual ShaderFormat shaderFormat() const = 0;

        /// Validates the bytecode as far as the backend can before a pipeline exists (Metal
        /// loads the library and looks up the entry point). Returns nullptr and logs on failure.
        [[nodiscard]] virtual std::unique_ptr<Shader> createShader(const ShaderDesc& desc) = 0;

        /// Compiles the pipeline. Can take milliseconds, so pipelines are created at load time,
        /// never per frame. Returns nullptr and logs on failure.
        [[nodiscard]] virtual std::unique_ptr<Pipeline> createPipeline(const PipelineDesc& desc) = 0;

        /// Records commands into a one-off command list, submits it and blocks until the GPU has
        /// finished it. Meant for load-time work such as filling GPU-only buffers. If called while
        /// a frame is being recorded, this work runs before that frame's commands.
        virtual void submitImmediate(const std::function<void(CommandList&)>& record) = 0;

        /// One swapchain per window. Returns nullptr and logs on failure.
        [[nodiscard]] virtual std::unique_ptr<Swapchain> createSwapchain(Window& window, const SwapchainDesc& desc) = 0;

        /// Blocks until the GPU has finished the frame that last used this frame slot.
        [[nodiscard]] virtual CommandList& beginFrame(Swapchain& swapchain) = 0;

        /// Submits the frame's commands and presents the swapchain if its backbuffer was acquired.
        virtual void endFrame(Swapchain& swapchain) = 0;

        /// Blocks until all submitted GPU work has completed. Required before destroying
        /// resources the GPU may still be using.
        virtual void waitIdle() = 0;

        /// Index of the current frame slot, in [0, kMaxFramesInFlight).
        [[nodiscard]] uint32_t frameSlot() const { return m_frameSlot; }

        /// Number of frames begun since the device was created.
        [[nodiscard]] uint64_t frameNumber() const { return m_frameNumber; }

    protected:
        Device() = default;

        void advanceFrame()
        {
            m_frameSlot = (m_frameSlot + 1) % kMaxFramesInFlight;
            ++m_frameNumber;
        }

    private:
        uint32_t m_frameSlot = 0;
        uint64_t m_frameNumber = 0;
    };
}

#pragma once

#include "Core/Handle.h"
#include "RHI/RHITypes.h"

#include <functional>
#include <memory>
#include <string_view>

namespace arc
{
    class Window;
}

namespace arc::rhi
{
    class Buffer;
    class CommandList;
    class Pipeline;
    class Sampler;
    class Shader;
    struct PipelineDesc;
    struct ShaderDesc;
    class Swapchain;
    class Texture;

    /// The GPU, its command queue and the per-frame synchronisation.
    ///
    /// Each build has exactly one real backend: D3D12 on Windows, Metal on macOS. The Null
    /// backend does no GPU work and exists so the engine builds and runs headless elsewhere.
    ///
    /// Frame flow:
    ///   CommandList& cmd = device.beginFrame(swapchain);  // Waits until a frame slot is free.
    ///   ...record...
    ///   device.endFrame(swapchain);                       // Submits and presents.
    ///
    /// Resource lifetime: buffers, textures and samplers are owned by whoever holds their
    /// unique_ptr, and may be destroyed at any time, including while frames in flight still use
    /// them. Each backend keeps the GPU object alive until the GPU has finished with it.
    class Device
    {
    public:
        /// Returns nullptr and logs the reason if no suitable GPU is found.
        [[nodiscard]] static std::unique_ptr<Device> create(const DeviceDesc& desc);

        /// Logs every buffer, texture and sampler still alive: each one is a leak, since resources
        /// must be destroyed before their device.
        virtual ~Device();

        Device(const Device&) = delete;
        Device& operator=(const Device&) = delete;

        [[nodiscard]] virtual const AdapterInfo& adapterInfo() const = 0;

        /// Upload and Readback buffers are mapped on creation. GPU-only buffers start in the Common
        /// state. Returns nullptr and logs on failure.
        [[nodiscard]] std::unique_ptr<Buffer> createBuffer(const BufferDesc& desc);

        /// GPU-only memory, starting in the Common state with undefined contents. Fill it with
        /// uploadTexture() (RHI/TextureUtils.h). Returns nullptr and logs on failure.
        [[nodiscard]] std::unique_ptr<Texture> createTexture(const TextureDesc& desc);

        /// Returns nullptr and logs on failure.
        [[nodiscard]] std::unique_ptr<Sampler> createSampler(const SamplerDesc& desc);

        /// The resource a handle refers to, or nullptr once it has been destroyed.
        [[nodiscard]] Buffer* find(BufferHandle handle) const;
        [[nodiscard]] Texture* find(TextureHandle handle) const;
        [[nodiscard]] Sampler* find(SamplerHandle handle) const;

        struct ResourceCounts
        {
            uint32_t buffers = 0;
            uint32_t textures = 0;     // Not counting swapchain backbuffers.
            uint32_t samplers = 0;
            /// Destroyed by their owners but kept until the GPU has finished with them.
            uint32_t awaitingRelease = 0;
        };

        [[nodiscard]] ResourceCounts resourceCounts() const;

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

        /// The GPU capture tool that captureNextFrame() will use, such as "RenderDoc", "PIX" or
        /// "Xcode", or empty if no tool is attached.
        [[nodiscard]] virtual std::string_view gpuCaptureTool() const { return {}; }

        /// Captures the next whole frame with gpuCaptureTool(). Does nothing if there is none.
        virtual void captureNextFrame() {}

        /// Index of the current frame slot, in [0, kMaxFramesInFlight).
        [[nodiscard]] uint32_t frameSlot() const { return m_frameSlot; }

        /// Number of frames begun since the device was created.
        [[nodiscard]] uint64_t frameNumber() const { return m_frameNumber; }

    protected:
        Device() = default;

        // Backend implementations of the create functions. The description has been validated.
        [[nodiscard]] virtual std::unique_ptr<Buffer> createBufferImpl(const BufferDesc& desc) = 0;
        [[nodiscard]] virtual std::unique_ptr<Texture> createTextureImpl(const TextureDesc& desc) = 0;
        [[nodiscard]] virtual std::unique_ptr<Sampler> createSamplerImpl(const SamplerDesc& desc) = 0;

        /// GPU objects whose owners have destroyed them but which the GPU may still be using.
        [[nodiscard]] virtual uint32_t awaitingReleaseCount() const { return 0; }

        void advanceFrame()
        {
            m_frameSlot = (m_frameSlot + 1) % kMaxFramesInFlight;
            ++m_frameNumber;
        }

    private:
        // The destructors of the resource classes remove themselves from the registry.
        friend class Buffer;
        friend class Sampler;
        friend class Texture;

        uint32_t m_frameSlot = 0;
        uint64_t m_frameNumber = 0;

        // Every live resource created through this device, for find() and the leak report.
        HandlePool<Buffer*, Buffer> m_buffers;
        HandlePool<Texture*, Texture> m_textures;
        HandlePool<Sampler*, Sampler> m_samplers;
    };
}

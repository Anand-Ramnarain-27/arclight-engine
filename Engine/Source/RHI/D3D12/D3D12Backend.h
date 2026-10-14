#pragma once

#include "RHI/Buffer.h"
#include "RHI/CommandList.h"
#include "RHI/D3D12/D3D12Common.h"
#include "RHI/Device.h"
#include "RHI/Pipeline.h"
#include "RHI/Shader.h"
#include "RHI/Swapchain.h"
#include "RHI/Texture.h"

#include <array>
#include <memory>
#include <string>
#include <vector>

namespace arc::rhi::d3d12
{
    class D3D12Device;

    class D3D12Texture final : public Texture
    {
    public:
        D3D12Texture() = default;

        void reset(ComPtr<ID3D12Resource> resource, const TextureDesc& desc, D3D12_RESOURCE_STATES state);
        void release();

        [[nodiscard]] ID3D12Resource* resource() const { return m_resource.Get(); }

        /// The state the resource is in at the current point of command recording. Valid because
        /// there is a single command list per frame on a single queue.
        D3D12_RESOURCE_STATES state = D3D12_RESOURCE_STATE_COMMON;

        /// Zero if the texture cannot be a render target.
        D3D12_CPU_DESCRIPTOR_HANDLE rtv{};

    private:
        ComPtr<ID3D12Resource> m_resource;
    };

    class D3D12Buffer final : public Buffer
    {
    public:
        D3D12Buffer(ComPtr<ID3D12Resource> resource, const BufferDesc& desc, void* mapped, ResourceState state)
            : m_resource(std::move(resource))
        {
            m_desc = desc;
            m_mapped = mapped;
            m_state = state;
        }

        [[nodiscard]] ID3D12Resource* resource() const { return m_resource.Get(); }
        [[nodiscard]] D3D12_GPU_VIRTUAL_ADDRESS gpuAddress() const { return m_resource->GetGPUVirtualAddress(); }

    private:
        ComPtr<ID3D12Resource> m_resource;
    };

    [[nodiscard]] D3D12_RESOURCE_STATES toD3D12State(ResourceState state);

    /// D3D12 has no shader object; the bytecode is passed to pipeline creation, so it is kept here.
    class D3D12Shader final : public Shader
    {
    public:
        explicit D3D12Shader(const ShaderDesc& desc)
            : Shader(desc)
            , m_bytecode(desc.bytecode.begin(), desc.bytecode.end())
        {
        }

        [[nodiscard]] D3D12_SHADER_BYTECODE bytecode() const { return {m_bytecode.data(), m_bytecode.size()}; }

    private:
        std::vector<std::byte> m_bytecode;
    };

    class D3D12Pipeline final : public Pipeline
    {
    public:
        D3D12Pipeline(const PipelineDesc& desc, ComPtr<ID3D12PipelineState> state, ID3D12RootSignature* rootSignature)
            : Pipeline(desc)
            , m_state(std::move(state))
            , m_rootSignature(rootSignature)
        {
        }

        [[nodiscard]] ID3D12PipelineState* state() const { return m_state.Get(); }
        /// Owned by the device.
        [[nodiscard]] ID3D12RootSignature* rootSignature() const { return m_rootSignature; }

    private:
        ComPtr<ID3D12PipelineState> m_state;
        ID3D12RootSignature* m_rootSignature = nullptr;
    };

    class D3D12CommandList final : public CommandList
    {
    public:
        [[nodiscard]] bool init(ID3D12Device* device, ID3D12CommandAllocator* allocator, std::string_view name);

        /// The allocator must not be in use by the GPU.
        void begin(ID3D12CommandAllocator* allocator);
        [[nodiscard]] bool close();
        void release() { m_list.Reset(); }

        void beginRenderPass(const RenderPassDesc& desc) override;
        void endRenderPass() override;
        void setPipeline(Pipeline& pipeline) override;
        void setVertexBuffer(Buffer& buffer, uint64_t offset) override;
        void setIndexBuffer(Buffer& buffer, IndexFormat format, uint64_t offset) override;
        void draw(uint32_t vertexCount, uint32_t instanceCount, uint32_t firstVertex, uint32_t firstInstance) override;
        void drawIndexed(uint32_t indexCount, uint32_t instanceCount, uint32_t firstIndex, int32_t baseVertex,
                         uint32_t firstInstance) override;
        void copyBuffer(Buffer& dst, uint64_t dstOffset, Buffer& src, uint64_t srcOffset, uint64_t size) override;
        void barrier(Buffer& buffer, ResourceState state) override;
        void pushDebugGroup(std::string_view name) override;
        void popDebugGroup() override;

        void transition(D3D12Texture& texture, D3D12_RESOURCE_STATES state);

        [[nodiscard]] ID3D12GraphicsCommandList* native() const { return m_list.Get(); }

    private:
        ComPtr<ID3D12GraphicsCommandList> m_list;
        D3D12Pipeline* m_pipeline = nullptr;
        bool m_inRenderPass = false;
        bool m_passHasDebugGroup = false;
        int m_debugDepth = 0;
    };

    class D3D12Swapchain final : public Swapchain
    {
    public:
        // Three buffers so the CPU can record a frame while one is displayed and one is queued.
        static constexpr uint32_t kBufferCount = 3;

        explicit D3D12Swapchain(D3D12Device& device);
        ~D3D12Swapchain() override;

        [[nodiscard]] bool init(Window& window, const SwapchainDesc& desc);

        [[nodiscard]] bool resize(Extent2D size) override;
        [[nodiscard]] Texture* acquireBackbuffer() override;
        void setVSync(bool enabled) override { m_desc.vsync = enabled; }

        /// Waits on the frame-latency waitable object. See SetMaximumFrameLatency.
        void waitUntilReady() override;

        /// The backbuffer returned by acquireBackbuffer() this frame, or nullptr.
        [[nodiscard]] D3D12Texture* acquiredBackbuffer() const { return m_acquired; }

        /// Must be called after the frame's command list has been submitted.
        [[nodiscard]] bool present();

    private:
        [[nodiscard]] bool createBackbufferViews();
        void releaseBackbuffers();
        [[nodiscard]] UINT swapchainFlags() const;

        D3D12Device& m_device;
        ComPtr<IDXGISwapChain3> m_swapchain;
        ComPtr<ID3D12DescriptorHeap> m_rtvHeap;
        HANDLE m_frameLatencyWaitable = nullptr;
        std::array<D3D12Texture, kBufferCount> m_backbuffers;
        D3D12Texture* m_acquired = nullptr;
    };

    class D3D12Device final : public Device
    {
    public:
        D3D12Device() = default;
        ~D3D12Device() override;

        [[nodiscard]] bool init(const DeviceDesc& desc);

        [[nodiscard]] const AdapterInfo& adapterInfo() const override { return m_adapterInfo; }
        [[nodiscard]] std::unique_ptr<Buffer> createBuffer(const BufferDesc& desc) override;
        [[nodiscard]] ShaderFormat shaderFormat() const override { return ShaderFormat::DXIL; }
        [[nodiscard]] std::unique_ptr<Shader> createShader(const ShaderDesc& desc) override;
        [[nodiscard]] std::unique_ptr<Pipeline> createPipeline(const PipelineDesc& desc) override;
        void submitImmediate(const std::function<void(CommandList&)>& record) override;
        [[nodiscard]] std::unique_ptr<Swapchain> createSwapchain(Window& window, const SwapchainDesc& desc) override;
        [[nodiscard]] CommandList& beginFrame(Swapchain& swapchain) override;
        void endFrame(Swapchain& swapchain) override;
        void waitIdle() override;

        [[nodiscard]] ID3D12Device* native() const { return m_device.Get(); }
        [[nodiscard]] ID3D12CommandQueue* queue() const { return m_queue.Get(); }
        [[nodiscard]] IDXGIFactory6* factory() const { return m_factory.Get(); }
        [[nodiscard]] bool tearingSupported() const { return m_tearingSupported; }

        /// Logs why the device was removed, if it was. Call after a call fails with DXGI_ERROR_DEVICE_REMOVED.
        void reportDeviceRemoved() const;

    private:
        struct FrameContext
        {
            ComPtr<ID3D12CommandAllocator> allocator;
            uint64_t fenceValue = 0;   // Signalled when the GPU finishes this slot's last frame.
        };

        [[nodiscard]] bool createFactory(bool debug);
        [[nodiscard]] bool createRootSignature();
        [[nodiscard]] bool selectAdapter();
        void configureInfoQueue();
        [[nodiscard]] uint64_t signal();
        void waitForFence(uint64_t value);

        ComPtr<IDXGIFactory6> m_factory;
        ComPtr<IDXGIAdapter4> m_adapter;
        ComPtr<ID3D12Device> m_device;
        ComPtr<ID3D12CommandQueue> m_queue;
        ComPtr<ID3D12Fence> m_fence;
        HANDLE m_fenceEvent = nullptr;
        uint64_t m_lastSignalledValue = 0;

        std::array<FrameContext, kMaxFramesInFlight> m_frames;
        D3D12CommandList m_commandList;

        // Shared by every pipeline until shaders bind resources. Empty apart from allowing the
        // input assembler to read vertex buffers.
        ComPtr<ID3D12RootSignature> m_rootSignature;

        // submitImmediate() waits for its work to finish, so one allocator suffices.
        ComPtr<ID3D12CommandAllocator> m_immediateAllocator;
        D3D12CommandList m_immediateList;

        AdapterInfo m_adapterInfo;
        bool m_debugLayer = false;
        bool m_tearingSupported = false;
        DWORD m_messageCallbackCookie = 0;
    };
}

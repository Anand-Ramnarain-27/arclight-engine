#pragma once

#include "RHI/Buffer.h"
#include "RHI/CommandList.h"
#include "Core/Handle.h"
#include "RHI/D3D12/D3D12Common.h"
#include "RHI/DeferredRelease.h"
#include "RHI/Device.h"
#include "RHI/Pipeline.h"
#include "RHI/Sampler.h"
#include "RHI/Shader.h"
#include "RHI/Swapchain.h"
#include "RHI/Texture.h"
#include "RHI/UploadRing.h"

#include <array>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace arc::rhi::d3d12
{
    class D3D12Device;

    struct D3D12Descriptor
    {
        D3D12_CPU_DESCRIPTOR_HANDLE cpu{};
        D3D12_GPU_DESCRIPTOR_HANDLE gpu{};   // Zero unless the heap is shader-visible.

        /// Identifies a persistent descriptor's slot and generation, so freeing it twice, or
        /// freeing a stale copy, is caught. Null for transient descriptors.
        Handle<D3D12Descriptor> handle;

        [[nodiscard]] bool valid() const { return cpu.ptr != 0; }
    };

    /// One D3D12 descriptor heap, split into two parts:
    ///   persistent  Single descriptors from a free list, returned with free(). For views that
    ///               live as long as their resource, and for ImGui's textures.
    ///   transient   One region per frame slot plus one for immediate submissions. Descriptors are
    ///               allocated linearly in contiguous runs and the whole region is reset at once.
    ///               For the descriptor tables a command list builds while it records.
    class D3D12DescriptorHeap
    {
    public:
        struct Desc
        {
            D3D12_DESCRIPTOR_HEAP_TYPE type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
            uint32_t persistentCount = 0;
            uint32_t transientCountPerRegion = 0;
            uint32_t transientRegions = 0;
            bool shaderVisible = false;
            std::string_view debugName{};
        };

        [[nodiscard]] bool init(ID3D12Device* device, const Desc& desc);
        void release();

        /// Returns an invalid descriptor and logs if the persistent part is full.
        [[nodiscard]] D3D12Descriptor allocate();

        /// Does nothing for an invalid descriptor. Asserts if it was already freed.
        void free(const D3D12Descriptor& descriptor);

        /// For callers that only kept the CPU handle, such as ImGui's free callback. Still catches
        /// a double free, but not a stale copy.
        void free(D3D12_CPU_DESCRIPTOR_HANDLE cpu);

        /// The GPU must have finished every command list that used the region.
        void resetTransient(uint32_t region);

        /// `count` contiguous descriptors. Returns an invalid descriptor and logs if the region is full.
        [[nodiscard]] D3D12Descriptor allocateTransient(uint32_t region, uint32_t count);

        [[nodiscard]] ID3D12DescriptorHeap* native() const { return m_heap.Get(); }
        [[nodiscard]] uint32_t persistentInUse() const { return m_persistentSlots.aliveCount(); }

    private:
        [[nodiscard]] D3D12Descriptor at(uint32_t index) const;

        ComPtr<ID3D12DescriptorHeap> m_heap;
        D3D12_CPU_DESCRIPTOR_HANDLE m_cpuStart{};
        D3D12_GPU_DESCRIPTOR_HANDLE m_gpuStart{};
        uint32_t m_stride = 0;
        uint32_t m_persistentCount = 0;
        uint32_t m_transientCountPerRegion = 0;
        HandleAllocator<D3D12Descriptor> m_persistentSlots{0};
        std::vector<uint32_t> m_transientUsed;   // Per region.
        std::string m_name;
    };

    class D3D12Texture final : public Texture
    {
    public:
        /// For swapchain backbuffers, which get their resource and view from the swapchain.
        D3D12Texture() = default;

        /// Takes ownership of the views, which are freed with the texture.
        D3D12Texture(D3D12Device& device, ComPtr<ID3D12Resource> resource, const TextureDesc& desc,
                     const D3D12Descriptor& srv, const D3D12Descriptor& rtv);
        ~D3D12Texture() override;

        void reset(ComPtr<ID3D12Resource> resource, const TextureDesc& desc, ResourceState state);
        void release();

        [[nodiscard]] ID3D12Resource* resource() const { return m_resource.Get(); }

        /// In the device's CPU-only SRV heap; copied into a shader-visible table at draw time.
        /// Zero if the texture is not Sampled.
        D3D12_CPU_DESCRIPTOR_HANDLE srv{};

        /// Zero if the texture cannot be a render target.
        D3D12_CPU_DESCRIPTOR_HANDLE rtv{};

    private:
        ComPtr<ID3D12Resource> m_resource;
        D3D12Device* m_device = nullptr;   // Null for backbuffers, whose views the swapchain owns.
        D3D12Descriptor m_srvDescriptor;
        D3D12Descriptor m_rtvDescriptor;
    };

    class D3D12Sampler final : public Sampler
    {
    public:
        D3D12Sampler(D3D12Device& device, const SamplerDesc& desc, const D3D12Descriptor& descriptor);
        ~D3D12Sampler() override;

        /// In the device's CPU-only sampler heap; copied into a shader-visible table at draw time.
        [[nodiscard]] D3D12_CPU_DESCRIPTOR_HANDLE descriptor() const { return m_descriptor.cpu; }

    private:
        D3D12Device& m_device;
        D3D12Descriptor m_descriptor;
    };

    class D3D12Buffer final : public Buffer
    {
    public:
        D3D12Buffer(D3D12Device& device, ComPtr<ID3D12Resource> resource, const BufferDesc& desc, void* mapped,
                    ResourceState state)
            : m_device(device)
            , m_resource(std::move(resource))
        {
            m_desc = desc;
            m_mapped = mapped;
            m_state = state;
        }
        ~D3D12Buffer() override;

        [[nodiscard]] ID3D12Resource* resource() const { return m_resource.Get(); }
        [[nodiscard]] D3D12_GPU_VIRTUAL_ADDRESS gpuAddress() const { return m_resource->GetGPUVirtualAddress(); }

    private:
        D3D12Device& m_device;
        ComPtr<ID3D12Resource> m_resource;
    };

    [[nodiscard]] D3D12_RESOURCE_STATES toD3D12State(ResourceState state);

    /// Root parameters of the root signature every pipeline shares.
    enum RootParameter : UINT
    {
        kRootTextureTable = 0,   // SRV table, t0 .. t(kMaxTextureSlots - 1).
        kRootSamplerTable = 1,   // Sampler table, s0 .. s(kMaxSamplerSlots - 1).
        kRootFirstConstants = 2, // Root CBVs b0 .. b(kMaxConstantSlots - 1), one parameter each.
        kRootParameterCount = kRootFirstConstants + kMaxConstantSlots,
    };

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
        D3D12Pipeline(D3D12Device& device, const PipelineDesc& desc, ComPtr<ID3D12PipelineState> state,
                      ID3D12RootSignature* rootSignature)
            : Pipeline(desc)
            , m_device(device)
            , m_state(std::move(state))
            , m_rootSignature(rootSignature)
        {
        }
        ~D3D12Pipeline() override;

        [[nodiscard]] ID3D12PipelineState* state() const { return m_state.Get(); }
        /// Owned by the device.
        [[nodiscard]] ID3D12RootSignature* rootSignature() const { return m_rootSignature; }

    private:
        D3D12Device& m_device;
        ComPtr<ID3D12PipelineState> m_state;
        ID3D12RootSignature* m_rootSignature = nullptr;
    };

    class D3D12CommandList final : public CommandList
    {
    public:
        [[nodiscard]] bool init(D3D12Device& device, ID3D12CommandAllocator* allocator, std::string_view name);

        /// The allocator must not be in use by the GPU. Descriptor tables are built in the given
        /// transient region of the device's shader-visible heaps, which must have been reset.
        void begin(ID3D12CommandAllocator* allocator, uint32_t transientRegion);
        [[nodiscard]] bool close();
        void release() { m_list.Reset(); }

        void beginRenderPass(const RenderPassDesc& desc) override;
        void endRenderPass() override;
        void setPipeline(Pipeline& pipeline) override;
        void setVertexBuffer(Buffer& buffer, uint64_t offset) override;
        void setIndexBuffer(Buffer& buffer, IndexFormat format, uint64_t offset) override;
        void setTexture(uint32_t slot, Texture& texture) override;
        void setSampler(uint32_t slot, Sampler& sampler) override;
        void draw(uint32_t vertexCount, uint32_t instanceCount, uint32_t firstVertex, uint32_t firstInstance) override;
        void drawIndexed(uint32_t indexCount, uint32_t instanceCount, uint32_t firstIndex, int32_t baseVertex,
                         uint32_t firstInstance) override;
        void copyBuffer(Buffer& dst, uint64_t dstOffset, Buffer& src, uint64_t srcOffset, uint64_t size) override;
        void copyBufferToTexture(Texture& dst, uint32_t mipLevel, Buffer& src, uint64_t srcOffset,
                                 uint32_t srcRowPitch) override;
        void copyTextureToBuffer(Buffer& dst, uint64_t dstOffset, uint32_t dstRowPitch, Texture& src,
                                 uint32_t mipLevel) override;
        void barrier(Buffer& buffer, ResourceState state) override;
        void barrier(Texture& texture, ResourceState state) override;
        void pushDebugGroup(std::string_view name) override;
        void popDebugGroup() override;

        /// For code that records into native() directly and changes the root signature or its
        /// arguments behind the command list's back, such as the ImGui backend.
        void invalidateBindings();

        [[nodiscard]] ID3D12GraphicsCommandList* native() const { return m_list.Get(); }

    protected:
        void bindConstants(uint32_t slot, Buffer& buffer, uint64_t offset) override;

    private:
        void resetBindings();

        /// Copies the bound descriptors into fresh shader-visible tables if any binding changed
        /// since the last draw. Returns false if the transient heap region is full.
        [[nodiscard]] bool flushBindings();

        ComPtr<ID3D12GraphicsCommandList> m_list;
        D3D12Device* m_device = nullptr;
        D3D12Pipeline* m_pipeline = nullptr;
        ID3D12RootSignature* m_rootSignature = nullptr;   // As last set on the native list.
        uint32_t m_transientRegion = 0;
        std::array<D3D12_CPU_DESCRIPTOR_HANDLE, kMaxTextureSlots> m_textures{};
        std::array<D3D12_CPU_DESCRIPTOR_HANDLE, kMaxSamplerSlots> m_samplers{};
        std::array<D3D12_GPU_VIRTUAL_ADDRESS, kMaxConstantSlots> m_constants{};
        bool m_texturesDirty = true;
        bool m_samplersDirty = true;
        uint32_t m_constantsDirty = 0;   // Bit per slot.
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
        [[nodiscard]] ShaderFormat shaderFormat() const override { return ShaderFormat::DXIL; }
        [[nodiscard]] std::unique_ptr<Shader> createShader(const ShaderDesc& desc) override;
        [[nodiscard]] std::unique_ptr<Pipeline> createPipeline(const PipelineDesc& desc) override;
        void submitImmediate(const std::function<void(CommandList&)>& record) override;
        [[nodiscard]] std::unique_ptr<Swapchain> createSwapchain(Window& window, const SwapchainDesc& desc) override;
        [[nodiscard]] CommandList& beginFrame(Swapchain& swapchain) override;
        void endFrame(Swapchain& swapchain) override;
        void waitIdle() override;

        [[nodiscard]] std::string_view gpuCaptureTool() const override;
        void captureNextFrame() override;

        [[nodiscard]] ID3D12Device* native() const { return m_device.Get(); }
        [[nodiscard]] ID3D12CommandQueue* queue() const { return m_queue.Get(); }
        [[nodiscard]] IDXGIFactory6* factory() const { return m_factory.Get(); }
        [[nodiscard]] bool tearingSupported() const { return m_tearingSupported; }

        /// Keeps a GPU object alive until every command list that may use it has completed. Called
        /// by the destructors of buffers, textures and pipelines.
        void retire(ComPtr<ID3D12Pageable> object);

        /// CPU-only heaps where views are created. Shaders never read these directly.
        [[nodiscard]] D3D12DescriptorHeap& cpuSrvHeap() { return m_cpuSrvHeap; }
        [[nodiscard]] D3D12DescriptorHeap& cpuSamplerHeap() { return m_cpuSamplerHeap; }
        [[nodiscard]] D3D12DescriptorHeap& rtvHeap() { return m_rtvHeap; }

        /// The two heaps bound on every command list. Only one CBV/SRV/UAV heap and one sampler heap
        /// can be bound at a time, so everything shaders read, ImGui included, comes from these.
        [[nodiscard]] D3D12DescriptorHeap& shaderVisibleSrvHeap() { return m_shaderVisibleSrvHeap; }
        [[nodiscard]] D3D12DescriptorHeap& shaderVisibleSamplerHeap() { return m_shaderVisibleSamplerHeap; }

        /// Stand-ins copied into the slots a pass has not bound: a null SRV, which reads as zero,
        /// and a point/clamp sampler.
        [[nodiscard]] D3D12_CPU_DESCRIPTOR_HANDLE nullSrv() const { return m_nullSrv.cpu; }
        [[nodiscard]] D3D12_CPU_DESCRIPTOR_HANDLE defaultSampler() const { return m_defaultSampler.cpu; }

        /// Per-frame memory for setConstants().
        [[nodiscard]] UploadRing& uploadRing() { return *m_uploadRing; }

        /// Logs why the device was removed, if it was. Call after a call fails with DXGI_ERROR_DEVICE_REMOVED.
        void reportDeviceRemoved() const;

    protected:
        [[nodiscard]] std::unique_ptr<Buffer> createBufferImpl(const BufferDesc& desc) override;
        [[nodiscard]] std::unique_ptr<Texture> createTextureImpl(const TextureDesc& desc) override;
        [[nodiscard]] std::unique_ptr<Sampler> createSamplerImpl(const SamplerDesc& desc) override;
        [[nodiscard]] uint32_t awaitingReleaseCount() const override
        {
            return static_cast<uint32_t>(m_deferredRelease.size());
        }

    private:
        struct FrameContext
        {
            ComPtr<ID3D12CommandAllocator> allocator;
            uint64_t fenceValue = 0;   // Signalled when the GPU finishes this slot's last frame.
        };

        void loadPixCapturer();
        void connectRenderDoc();
        [[nodiscard]] bool createFactory(bool debug);
        [[nodiscard]] bool createDescriptorHeaps();
        [[nodiscard]] bool createDefaultDescriptors();
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

        // Fence values tag each retired object; see DeferredReleaseQueue.
        DeferredReleaseQueue<ComPtr<ID3D12Pageable>> m_deferredRelease;
        bool m_frameOpen = false;   // Between beginFrame() and endFrame().

        std::array<FrameContext, kMaxFramesInFlight> m_frames;
        D3D12CommandList m_commandList;

        // Shared by every pipeline; see RootParameter. One layout for all shaders keeps the RHI's
        // slot model identical to Metal's, where every stage has fixed texture and sampler slots.
        ComPtr<ID3D12RootSignature> m_rootSignature;

        D3D12DescriptorHeap m_cpuSrvHeap;
        D3D12DescriptorHeap m_cpuSamplerHeap;
        D3D12DescriptorHeap m_rtvHeap;
        D3D12DescriptorHeap m_shaderVisibleSrvHeap;
        D3D12DescriptorHeap m_shaderVisibleSamplerHeap;
        D3D12Descriptor m_nullSrv;
        D3D12Descriptor m_defaultSampler;
        std::unique_ptr<UploadRing> m_uploadRing;

        // submitImmediate() waits for its work to finish, so one allocator suffices.
        ComPtr<ID3D12CommandAllocator> m_immediateAllocator;
        D3D12CommandList m_immediateList;

        // In-app capture triggers, resolved at run time from DLLs the tools load. Both are null
        // unless the editor was started from RenderDoc, or with --pix and PIX installed.
        void* m_renderDoc = nullptr;   // RENDERDOC_API_1_1_2*
        using PixGpuCaptureNextFrames = HRESULT(WINAPI*)(PCWSTR fileName, UINT32 numFrames);
        PixGpuCaptureNextFrames m_pixCaptureNextFrames = nullptr;

        AdapterInfo m_adapterInfo;
        bool m_debugLayer = false;
        bool m_tearingSupported = false;
        DWORD m_messageCallbackCookie = 0;
    };
}

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

        /// Takes ownership of the views, which are freed with the texture. `rtvs` holds one view per
        /// subresource, indexed by subresourceIndex(), or is empty.
        D3D12Texture(D3D12Device& device, ComPtr<ID3D12Resource> resource, const TextureDesc& desc,
                     const D3D12Descriptor& srv, std::vector<D3D12Descriptor> rtvs, const D3D12Descriptor& dsv);
        ~D3D12Texture() override;

        void reset(ComPtr<ID3D12Resource> resource, const TextureDesc& desc, ResourceState state);
        void release();

        [[nodiscard]] ID3D12Resource* resource() const { return m_resource.Get(); }

        /// In the device's CPU-only SRV heap; copied into a shader-visible table at draw time.
        /// Zero if the texture is not Sampled.
        D3D12_CPU_DESCRIPTOR_HANDLE srv{};

        /// The view of mip level 0 of layer 0. Zero if the texture cannot be a render target.
        D3D12_CPU_DESCRIPTOR_HANDLE rtv{};

        /// The view of one mip level of one array layer, for rendering into a cube face or a mip.
        [[nodiscard]] D3D12_CPU_DESCRIPTOR_HANDLE rtvFor(uint32_t mipLevel, uint32_t arrayLayer) const;

        /// Zero if the texture cannot be a depth target.
        D3D12_CPU_DESCRIPTOR_HANDLE dsv{};

        /// Takes ownership of one unordered-access view per mip level, for Storage textures.
        void setUavs(std::vector<D3D12Descriptor> uavs) { m_uavDescriptors = std::move(uavs); }

        /// The unordered-access view of one mip level. Zero unless the texture has Storage usage.
        [[nodiscard]] D3D12_CPU_DESCRIPTOR_HANDLE uavFor(uint32_t mipLevel) const
        {
            return mipLevel < m_uavDescriptors.size() ? m_uavDescriptors[mipLevel].cpu : D3D12_CPU_DESCRIPTOR_HANDLE{};
        }

    private:
        ComPtr<ID3D12Resource> m_resource;
        D3D12Device* m_device = nullptr;   // Null for backbuffers, whose views the swapchain owns.
        D3D12Descriptor m_srvDescriptor;
        std::vector<D3D12Descriptor> m_rtvDescriptors;
        D3D12Descriptor m_dsvDescriptor;
        std::vector<D3D12Descriptor> m_uavDescriptors;
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
        /// Takes ownership of the view, which is freed with the buffer.
        D3D12Buffer(D3D12Device& device, ComPtr<ID3D12Resource> resource, const BufferDesc& desc, void* mapped,
                    ResourceState state, const D3D12Descriptor& srv)
            : m_device(device)
            , m_resource(std::move(resource))
            , m_srvDescriptor(srv)
        {
            m_desc = desc;
            m_mapped = mapped;
            m_state = state;
        }
        ~D3D12Buffer() override;

        [[nodiscard]] ID3D12Resource* resource() const { return m_resource.Get(); }
        [[nodiscard]] D3D12_GPU_VIRTUAL_ADDRESS gpuAddress() const { return m_resource->GetGPUVirtualAddress(); }

        /// A StructuredBuffer view in the device's CPU-only SRV heap, copied into a shader-visible
        /// table at draw time like a texture's. Zero unless the buffer has Structured usage.
        [[nodiscard]] D3D12_CPU_DESCRIPTOR_HANDLE srv() const { return m_srvDescriptor.cpu; }

        /// Takes ownership of a RWStructuredBuffer view, for Storage buffers.
        void setUav(const D3D12Descriptor& uav) { m_uavDescriptor = uav; }

        /// In the device's CPU-only SRV heap, copied into the storage table at dispatch time. Zero
        /// unless the buffer has Storage usage.
        [[nodiscard]] D3D12_CPU_DESCRIPTOR_HANDLE uav() const { return m_uavDescriptor.cpu; }

    private:
        D3D12Device& m_device;
        ComPtr<ID3D12Resource> m_resource;
        D3D12Descriptor m_srvDescriptor;
        D3D12Descriptor m_uavDescriptor;
    };

    [[nodiscard]] D3D12_RESOURCE_STATES toD3D12State(ResourceState state);

    /// Root parameters of the root signature every pipeline shares.
    enum RootParameter : UINT
    {
        kRootTextureTable = 0,   // SRV table, t0 .. t(kMaxTextureSlots - 1): textures and structured buffers.
        kRootSamplerTable = 1,   // Sampler table, s0 .. s(kMaxSamplerSlots - 1).
        kRootStorageTable = 2,   // UAV table, u0 .. u(kMaxStorageSlots - 1). Set by compute passes only.
        kRootFirstConstants = 3, // Root CBVs b0 .. b(kMaxConstantSlots - 1), one parameter each.
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

    class D3D12ComputePipeline final : public ComputePipeline
    {
    public:
        D3D12ComputePipeline(D3D12Device& device, const ComputePipelineDesc& desc, ComPtr<ID3D12PipelineState> state,
                             ID3D12RootSignature* rootSignature)
            : ComputePipeline(desc)
            , m_device(device)
            , m_state(std::move(state))
            , m_rootSignature(rootSignature)
        {
        }
        ~D3D12ComputePipeline() override;

        [[nodiscard]] ID3D12PipelineState* state() const { return m_state.Get(); }
        /// Owned by the device; the same root signature as the graphics pipelines.
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
        /// transient region of the device's shader-visible heaps, which must have been reset. With
        /// `timing`, each render pass writes its start and end timestamps into `timestampHeap` from
        /// `firstTimestamp` on.
        void begin(ID3D12CommandAllocator* allocator, uint32_t transientRegion, GpuTimingRecorder* timing = nullptr,
                   ID3D12QueryHeap* timestampHeap = nullptr, uint32_t firstTimestamp = 0);
        [[nodiscard]] bool close();
        void release() { m_list.Reset(); }

        void beginRenderPass(const RenderPassDesc& desc) override;
        void endRenderPass() override;
        void beginComputePass(const ComputePassDesc& desc) override;
        void endComputePass() override;
        void setPipeline(Pipeline& pipeline) override;
        void setComputePipeline(ComputePipeline& pipeline) override;
        void setStorageBuffer(uint32_t slot, Buffer& buffer) override;
        void setStorageTexture(uint32_t slot, Texture& texture, uint32_t mipLevel) override;
        void dispatch(uint32_t groupsX, uint32_t groupsY, uint32_t groupsZ) override;
        void storageBarrier() override;
        void setVertexBuffer(Buffer& buffer, uint64_t offset) override;
        void setIndexBuffer(Buffer& buffer, IndexFormat format, uint64_t offset) override;
        void setTexture(uint32_t slot, Texture& texture) override;
        void setBuffer(uint32_t slot, Buffer& buffer) override;
        void setSampler(uint32_t slot, Sampler& sampler) override;
        void draw(uint32_t vertexCount, uint32_t instanceCount, uint32_t firstVertex, uint32_t firstInstance) override;
        void drawIndexed(uint32_t indexCount, uint32_t instanceCount, uint32_t firstIndex, int32_t baseVertex,
                         uint32_t firstInstance) override;
        void copyBuffer(Buffer& dst, uint64_t dstOffset, Buffer& src, uint64_t srcOffset, uint64_t size) override;
        void copyBufferToTexture(Texture& dst, uint32_t mipLevel, uint32_t arrayLayer, Buffer& src, uint64_t srcOffset,
                                 uint32_t srcRowPitch) override;
        void copyTextureToBuffer(Buffer& dst, uint64_t dstOffset, uint32_t dstRowPitch, Texture& src, uint32_t mipLevel,
                                 uint32_t arrayLayer) override;
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
        [[nodiscard]] bool inPass() const { return m_inRenderPass || m_inComputePass; }
        void beginTimedPass(std::string_view name);
        void endTimedPass();

        /// Copies the bound descriptors into fresh shader-visible tables if any binding changed
        /// since the last draw or dispatch, and sets them on the graphics or the compute root
        /// signature. Returns false if the transient heap region is full.
        [[nodiscard]] bool flushBindings(bool compute);

        ComPtr<ID3D12GraphicsCommandList> m_list;
        D3D12Device* m_device = nullptr;
        D3D12Pipeline* m_pipeline = nullptr;
        ID3D12RootSignature* m_rootSignature = nullptr;   // As last set on the native list.
        D3D12ComputePipeline* m_computePipeline = nullptr;
        ID3D12RootSignature* m_computeRootSignature = nullptr;   // Compute has a root signature of its own.
        uint32_t m_transientRegion = 0;
        std::array<D3D12_CPU_DESCRIPTOR_HANDLE, kMaxTextureSlots> m_textures{};   // Texture or buffer SRVs.
        std::array<D3D12_CPU_DESCRIPTOR_HANDLE, kMaxSamplerSlots> m_samplers{};
        std::array<D3D12_CPU_DESCRIPTOR_HANDLE, kMaxStorageSlots> m_storage{};
        std::array<D3D12_GPU_VIRTUAL_ADDRESS, kMaxConstantSlots> m_constants{};
        bool m_texturesDirty = true;
        bool m_samplersDirty = true;
        bool m_storageDirty = true;
        uint32_t m_constantsDirty = 0;   // Bit per slot.
        bool m_inRenderPass = false;
        bool m_inComputePass = false;
        bool m_passHasDebugGroup = false;
        int m_debugDepth = 0;

        GpuTimingRecorder* m_timing = nullptr;
        ID3D12QueryHeap* m_timestampHeap = nullptr;
        uint32_t m_firstTimestamp = 0;
        uint32_t m_timedPass = GpuTimingRecorder::kNoPass;
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
        [[nodiscard]] std::unique_ptr<ComputePipeline> createComputePipeline(const ComputePipelineDesc& desc) override;
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
        [[nodiscard]] D3D12DescriptorHeap& dsvHeap() { return m_dsvHeap; }

        /// The two heaps bound on every command list. Only one CBV/SRV/UAV heap and one sampler heap
        /// can be bound at a time, so everything shaders read, ImGui included, comes from these.
        [[nodiscard]] D3D12DescriptorHeap& shaderVisibleSrvHeap() { return m_shaderVisibleSrvHeap; }
        [[nodiscard]] D3D12DescriptorHeap& shaderVisibleSamplerHeap() { return m_shaderVisibleSamplerHeap; }

        /// Stand-ins copied into the slots a pass has not bound: a null SRV, which reads as zero,
        /// and a point/clamp sampler.
        [[nodiscard]] D3D12_CPU_DESCRIPTOR_HANDLE nullSrv() const { return m_nullSrv.cpu; }
        /// A null unordered-access view for the storage slots a compute pass has not bound.
        [[nodiscard]] D3D12_CPU_DESCRIPTOR_HANDLE nullUav() const { return m_nullUav.cpu; }
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
        /// Leaves GPU timing unsupported, rather than failing, if the queue cannot report timestamps.
        void createTimestampQueries();
        /// Reads the timestamps of the frame that last used the current slot into the GPU timing recorder.
        void resolveTimestamps();
        [[nodiscard]] int64_t gpuClockNs() const;
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

        // GpuTimingRecorder::kTimestampsPerFrame timestamps per frame slot, resolved at the end of each
        // frame into the slot's part of the readback buffer.
        ComPtr<ID3D12QueryHeap> m_timestampHeap;
        std::unique_ptr<Buffer> m_timestampReadback;
        uint64_t m_timestampFrequency = 0;   // Ticks per second.
        std::array<uint32_t, kMaxFramesInFlight> m_timedPasses{};
        std::vector<int64_t> m_timestampsNs;

        // Shared by every pipeline; see RootParameter. One layout for all shaders keeps the RHI's
        // slot model identical to Metal's, where every stage has fixed texture and sampler slots.
        ComPtr<ID3D12RootSignature> m_rootSignature;

        D3D12DescriptorHeap m_cpuSrvHeap;
        D3D12DescriptorHeap m_cpuSamplerHeap;
        D3D12DescriptorHeap m_rtvHeap;
        D3D12DescriptorHeap m_dsvHeap;
        D3D12DescriptorHeap m_shaderVisibleSrvHeap;
        D3D12DescriptorHeap m_shaderVisibleSamplerHeap;
        D3D12Descriptor m_nullSrv;
        D3D12Descriptor m_nullUav;
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

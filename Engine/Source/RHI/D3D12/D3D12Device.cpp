#include "RHI/D3D12/D3D12Backend.h"

#include "Core/Assert.h"
#include "Core/Log.h"
#include "RHI/Backends.h"

#include <d3d12sdklayers.h>

namespace arc::rhi::d3d12
{
    namespace
    {
        // Feature level 12.0 is supported by every D3D12 GPU from the last decade
        // (NVIDIA Maxwell 2, AMD GCN 2, Intel Gen9 and newer).
        constexpr D3D_FEATURE_LEVEL kMinFeatureLevel = D3D_FEATURE_LEVEL_12_0;

        void CALLBACK onDebugMessage(D3D12_MESSAGE_CATEGORY /*category*/, D3D12_MESSAGE_SEVERITY severity,
                                     D3D12_MESSAGE_ID id, LPCSTR description, void* /*context*/)
        {
            const log::Level level = [severity] {
                switch (severity)
                {
                    case D3D12_MESSAGE_SEVERITY_CORRUPTION:
                    case D3D12_MESSAGE_SEVERITY_ERROR:   return log::Level::Error;
                    case D3D12_MESSAGE_SEVERITY_WARNING: return log::Level::Warning;
                    default:                             return log::Level::Info;
                }
            }();
            ARC_LOG_IMPL(level, "[D3D12 #{}] {}", static_cast<int>(id), description);
        }
    }

    D3D12Device::~D3D12Device()
    {
        if (m_device)
            waitIdle();

        if (m_fenceEvent)
            CloseHandle(m_fenceEvent);

        if (m_messageCallbackCookie != 0)
        {
            ComPtr<ID3D12InfoQueue1> infoQueue;
            if (SUCCEEDED(m_device.As(&infoQueue)))
                infoQueue->UnregisterMessageCallback(m_messageCallbackCookie);
        }

        m_commandList.release();
        m_immediateList.release();
        m_immediateAllocator.Reset();
        for (FrameContext& frame : m_frames)
            frame.allocator.Reset();
        m_fence.Reset();
        m_queue.Reset();

        // Every engine-owned D3D12 object is released by now, so anything the debug layer still
        // reports (apart from the device itself) is a leak.
        if (m_debugLayer)
        {
            ComPtr<ID3D12DebugDevice> debugDevice;
            if (SUCCEEDED(m_device.As(&debugDevice)))
                debugDevice->ReportLiveDeviceObjects(D3D12_RLDO_DETAIL | D3D12_RLDO_IGNORE_INTERNAL);
        }
    }

    bool D3D12Device::init(const DeviceDesc& desc)
    {
        m_debugLayer = desc.debugLayer;

        // The debug layer must be enabled before the device is created.
        if (desc.debugLayer)
        {
            ComPtr<ID3D12Debug> debug;
            if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&debug))))
            {
                debug->EnableDebugLayer();
                ARC_LOG_INFO("D3D12 debug layer enabled");

                ComPtr<ID3D12Debug1> debug1;
                if (desc.gpuValidation && SUCCEEDED(debug.As(&debug1)))
                {
                    debug1->SetEnableGPUBasedValidation(TRUE);
                    ARC_LOG_INFO("D3D12 GPU-based validation enabled");
                }
            }
            else
            {
                // The layer ships with the "Graphics Tools" optional Windows feature.
                ARC_LOG_WARN("D3D12 debug layer unavailable; install the Windows 'Graphics Tools' optional feature");
                m_debugLayer = false;
            }
        }

        if (!createFactory(m_debugLayer) || !selectAdapter())
            return false;

        if (!succeeded(D3D12CreateDevice(m_adapter.Get(), kMinFeatureLevel, IID_PPV_ARGS(&m_device)), "D3D12CreateDevice"))
            return false;
        setDebugName(m_device.Get(), "Arclight Device");

        // Integrated GPUs share system memory with the CPU (UMA); DedicatedVideoMemory alone
        // cannot tell, since integrated GPUs often report a small dedicated carve-out.
        D3D12_FEATURE_DATA_ARCHITECTURE architecture{};
        if (SUCCEEDED(m_device->CheckFeatureSupport(D3D12_FEATURE_ARCHITECTURE, &architecture, sizeof(architecture))))
            m_adapterInfo.unifiedMemory = architecture.UMA == TRUE;

        if (m_debugLayer)
            configureInfoQueue();

        D3D12_COMMAND_QUEUE_DESC queueDesc{};
        queueDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
        if (!succeeded(m_device->CreateCommandQueue(&queueDesc, IID_PPV_ARGS(&m_queue)), "CreateCommandQueue"))
            return false;
        setDebugName(m_queue.Get(), "Direct Queue");

        if (!succeeded(m_device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&m_fence)), "CreateFence"))
            return false;
        setDebugName(m_fence.Get(), "Frame Fence");

        m_fenceEvent = CreateEventW(nullptr, FALSE, FALSE, nullptr);
        if (!m_fenceEvent)
        {
            ARC_LOG_ERROR("CreateEvent failed for the frame fence");
            return false;
        }

        for (uint32_t i = 0; i < kMaxFramesInFlight; ++i)
        {
            if (!succeeded(m_device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,
                                                            IID_PPV_ARGS(&m_frames[i].allocator)),
                           "CreateCommandAllocator"))
                return false;
            setDebugName(m_frames[i].allocator.Get(), std::format("Frame Allocator {}", i));
        }

        if (!succeeded(m_device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&m_immediateAllocator)),
                       "CreateCommandAllocator"))
            return false;
        setDebugName(m_immediateAllocator.Get(), "Immediate Allocator");

        return m_commandList.init(m_device.Get(), m_frames[0].allocator.Get(), "Frame Command List") &&
               m_immediateList.init(m_device.Get(), m_immediateAllocator.Get(), "Immediate Command List");
    }

    bool D3D12Device::createFactory(bool debug)
    {
        const UINT flags = debug ? DXGI_CREATE_FACTORY_DEBUG : 0;
        if (!succeeded(CreateDXGIFactory2(flags, IID_PPV_ARGS(&m_factory)), "CreateDXGIFactory2"))
            return false;

        // Tearing (presenting without vsync on a variable-refresh or windowed display) needs
        // explicit support from the OS and driver.
        ComPtr<IDXGIFactory5> factory5;
        BOOL allowTearing = FALSE;
        if (SUCCEEDED(m_factory.As(&factory5)) &&
            SUCCEEDED(factory5->CheckFeatureSupport(DXGI_FEATURE_PRESENT_ALLOW_TEARING, &allowTearing,
                                                    sizeof(allowTearing))))
            m_tearingSupported = allowTearing == TRUE;

        return true;
    }

    bool D3D12Device::selectAdapter()
    {
        // Adapters are enumerated fastest first, so laptops pick the discrete GPU over the integrated one.
        ComPtr<IDXGIAdapter4> adapter;
        for (UINT i = 0; m_factory->EnumAdapterByGpuPreference(i, DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE,
                                                               IID_PPV_ARGS(&adapter)) != DXGI_ERROR_NOT_FOUND;
             ++i)
        {
            DXGI_ADAPTER_DESC3 desc{};
            adapter->GetDesc3(&desc);

            if (desc.Flags & DXGI_ADAPTER_FLAG3_SOFTWARE)
                continue;

            // Passing a null device only checks whether creation would succeed.
            if (FAILED(D3D12CreateDevice(adapter.Get(), kMinFeatureLevel, __uuidof(ID3D12Device), nullptr)))
                continue;

            m_adapter = adapter;
            m_adapterInfo.backend = Backend::D3D12;
            m_adapterInfo.name = narrow(desc.Description);
            m_adapterInfo.videoMemoryMB = desc.DedicatedVideoMemory / (1024 * 1024);

            // The user-mode driver version, as shown in Device Manager (a.b.c.d).
            LARGE_INTEGER umdVersion{};
            if (SUCCEEDED(adapter->CheckInterfaceSupport(__uuidof(IDXGIDevice), &umdVersion)))
            {
                const auto part = [&](int shift) { return (umdVersion.QuadPart >> shift) & 0xFFFF; };
                m_adapterInfo.driver = std::format("{}.{}.{}.{}", part(48), part(32), part(16), part(0));
            }
            return true;
        }

        ARC_LOG_ERROR("No GPU supports Direct3D 12 at feature level 12_0");
        return false;
    }

    void D3D12Device::configureInfoQueue()
    {
        ComPtr<ID3D12InfoQueue> infoQueue;
        if (FAILED(m_device.As(&infoQueue)))
            return;

        // Breaking without a debugger attached would terminate the process.
        if (IsDebuggerPresent())
        {
            infoQueue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_CORRUPTION, TRUE);
            infoQueue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_ERROR, TRUE);
        }

        // Info-level messages are mostly object creation notices.
        D3D12_MESSAGE_SEVERITY hiddenSeverities[] = {D3D12_MESSAGE_SEVERITY_INFO, D3D12_MESSAGE_SEVERITY_MESSAGE};
        D3D12_INFO_QUEUE_FILTER filter{};
        filter.DenyList.NumSeverities = static_cast<UINT>(std::size(hiddenSeverities));
        filter.DenyList.pSeverityList = hiddenSeverities;
        infoQueue->PushStorageFilter(&filter);

        // ID3D12InfoQueue1 (Windows 11, or the Agility SDK) delivers messages through a callback,
        // which routes them into the engine log and the editor console. Without it, messages
        // only appear in the debugger output window.
        ComPtr<ID3D12InfoQueue1> infoQueue1;
        if (SUCCEEDED(m_device.As(&infoQueue1)) &&
            SUCCEEDED(infoQueue1->RegisterMessageCallback(onDebugMessage, D3D12_MESSAGE_CALLBACK_FLAG_NONE,
                                                          this, &m_messageCallbackCookie)))
            return;

        ARC_LOG_INFO("D3D12 debug messages go to the debugger output only (ID3D12InfoQueue1 unavailable)");
    }

    std::unique_ptr<Buffer> D3D12Device::createBuffer(const BufferDesc& desc)
    {
        ARC_ASSERT(desc.size > 0, "Buffer '{}' has zero size", desc.debugName);

        D3D12_HEAP_PROPERTIES heap{};
        D3D12_RESOURCE_STATES initialState = D3D12_RESOURCE_STATE_COMMON;
        ResourceState trackedState = ResourceState::Common;
        switch (desc.memory)
        {
            case MemoryType::GpuOnly:
                heap.Type = D3D12_HEAP_TYPE_DEFAULT;
                break;
            case MemoryType::Upload:
                // GENERIC_READ is the only state an upload heap resource may be in.
                heap.Type = D3D12_HEAP_TYPE_UPLOAD;
                initialState = D3D12_RESOURCE_STATE_GENERIC_READ;
                trackedState = ResourceState::CopySource;
                break;
            case MemoryType::Readback:
                heap.Type = D3D12_HEAP_TYPE_READBACK;
                initialState = D3D12_RESOURCE_STATE_COPY_DEST;
                trackedState = ResourceState::CopyDest;
                break;
        }

        D3D12_RESOURCE_DESC resourceDesc{};
        resourceDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
        resourceDesc.Width = desc.size;
        resourceDesc.Height = 1;
        resourceDesc.DepthOrArraySize = 1;
        resourceDesc.MipLevels = 1;
        resourceDesc.Format = DXGI_FORMAT_UNKNOWN;
        resourceDesc.SampleDesc.Count = 1;
        // Required for buffers.
        resourceDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

        // A committed resource gets its own implicit heap. Simple, at the cost of a 64 KB
        // minimum allocation per buffer; suballocating from shared heaps comes later.
        ComPtr<ID3D12Resource> resource;
        if (!succeeded(m_device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &resourceDesc, initialState,
                                                         nullptr, IID_PPV_ARGS(&resource)),
                       "CreateCommittedResource(buffer)"))
            return nullptr;
        setDebugName(resource.Get(), desc.debugName);

        // CPU-visible heaps stay mapped for the buffer's lifetime, which D3D12 allows. The empty
        // read range tells the driver the CPU will not read an upload buffer.
        void* mapped = nullptr;
        if (desc.memory != MemoryType::GpuOnly)
        {
            const D3D12_RANGE noRead{0, 0};
            const D3D12_RANGE* readRange = desc.memory == MemoryType::Upload ? &noRead : nullptr;
            if (!succeeded(resource->Map(0, readRange, &mapped), "ID3D12Resource::Map"))
                return nullptr;
        }

        return std::make_unique<D3D12Buffer>(std::move(resource), desc, mapped, trackedState);
    }

    void D3D12Device::submitImmediate(const std::function<void(CommandList&)>& record)
    {
        ARC_VERIFY(SUCCEEDED(m_immediateAllocator->Reset()));
        m_immediateList.begin(m_immediateAllocator.Get());
        record(m_immediateList);

        if (m_immediateList.close())
        {
            ID3D12CommandList* lists[] = {m_immediateList.native()};
            m_queue->ExecuteCommandLists(1, lists);
        }
        m_immediateList.decayBufferStates();
        waitForFence(signal());
    }

    std::unique_ptr<Swapchain> D3D12Device::createSwapchain(Window& window, const SwapchainDesc& desc)
    {
        auto swapchain = std::make_unique<D3D12Swapchain>(*this);
        if (!swapchain->init(window, desc))
            return nullptr;
        return swapchain;
    }

    CommandList& D3D12Device::beginFrame(Swapchain& /*swapchain*/)
    {
        // The allocator's memory is still referenced by the GPU until that frame's fence passes.
        FrameContext& frame = m_frames[frameSlot()];
        waitForFence(frame.fenceValue);

        ARC_VERIFY(SUCCEEDED(frame.allocator->Reset()));
        m_commandList.begin(frame.allocator.Get());
        return m_commandList;
    }

    void D3D12Device::endFrame(Swapchain& swapchain)
    {
        auto& d3dSwapchain = static_cast<D3D12Swapchain&>(swapchain);

        if (D3D12Texture* backbuffer = d3dSwapchain.acquiredBackbuffer())
            m_commandList.transition(*backbuffer, D3D12_RESOURCE_STATE_PRESENT);

        if (m_commandList.close())
        {
            ID3D12CommandList* lists[] = {m_commandList.native()};
            m_queue->ExecuteCommandLists(1, lists);
        }
        m_commandList.decayBufferStates();

        if (d3dSwapchain.acquiredBackbuffer() && !d3dSwapchain.present())
            reportDeviceRemoved();

        m_frames[frameSlot()].fenceValue = signal();
        advanceFrame();
    }

    void D3D12Device::waitIdle()
    {
        if (m_queue && m_fence)
            waitForFence(signal());
    }

    uint64_t D3D12Device::signal()
    {
        const uint64_t value = ++m_lastSignalledValue;
        ARC_VERIFY(SUCCEEDED(m_queue->Signal(m_fence.Get(), value)));
        return value;
    }

    void D3D12Device::waitForFence(uint64_t value)
    {
        if (m_fence->GetCompletedValue() >= value)
            return;

        ARC_VERIFY(SUCCEEDED(m_fence->SetEventOnCompletion(value, m_fenceEvent)));
        WaitForSingleObject(m_fenceEvent, INFINITE);
    }

    void D3D12Device::reportDeviceRemoved() const
    {
        const HRESULT reason = m_device->GetDeviceRemovedReason();
        if (reason != S_OK)
            (void)succeeded(reason, "GPU device removed");
    }
}

namespace arc::rhi
{
    std::unique_ptr<Device> createD3D12Device(const DeviceDesc& desc)
    {
        auto device = std::make_unique<d3d12::D3D12Device>();
        if (!device->init(desc))
            return nullptr;
        return device;
    }
}

#include "RHI/D3D12/D3D12Backend.h"

#include "Core/Assert.h"
#include "Core/Log.h"
#include "Platform/Window.h"

#include <SDL3/SDL.h>

namespace arc::rhi::d3d12
{
    void D3D12Texture::reset(ComPtr<ID3D12Resource> resource, const TextureDesc& desc, D3D12_RESOURCE_STATES initialState)
    {
        m_resource = std::move(resource);
        m_desc = desc;
        state = initialState;
    }

    void D3D12Texture::release()
    {
        m_resource.Reset();
        state = D3D12_RESOURCE_STATE_COMMON;
    }

    D3D12Swapchain::D3D12Swapchain(D3D12Device& device) : m_device(device) {}

    D3D12Swapchain::~D3D12Swapchain()
    {
        // The GPU may still be reading a backbuffer that is about to be released.
        m_device.waitIdle();
        releaseBackbuffers();
        if (m_frameLatencyWaitable)
            CloseHandle(m_frameLatencyWaitable);
    }

    UINT D3D12Swapchain::swapchainFlags() const
    {
        UINT flags = DXGI_SWAP_CHAIN_FLAG_FRAME_LATENCY_WAITABLE_OBJECT;
        if (m_device.tearingSupported())
            flags |= DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING;
        return flags;
    }

    bool D3D12Swapchain::init(Window& window, const SwapchainDesc& desc)
    {
        m_desc = desc;

        auto* hwnd = static_cast<HWND>(SDL_GetPointerProperty(SDL_GetWindowProperties(window.sdlWindow()),
                                                              SDL_PROP_WINDOW_WIN32_HWND_POINTER, nullptr));
        if (!hwnd)
        {
            ARC_LOG_ERROR("The window has no HWND: {}", SDL_GetError());
            return false;
        }

        // Flip model is required for D3D12. FLIP_DISCARD lets the driver skip preserving the
        // previous contents, which every frame overwrites anyway.
        DXGI_SWAP_CHAIN_DESC1 scDesc{};
        scDesc.Width = desc.size.width;
        scDesc.Height = desc.size.height;
        scDesc.Format = toDxgiFormat(desc.format);
        scDesc.SampleDesc.Count = 1;
        scDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
        scDesc.BufferCount = kBufferCount;
        scDesc.Scaling = DXGI_SCALING_STRETCH;
        scDesc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
        scDesc.AlphaMode = DXGI_ALPHA_MODE_IGNORE;
        scDesc.Flags = swapchainFlags();

        // For D3D12 the "device" argument is the command queue that presents.
        ComPtr<IDXGISwapChain1> swapchain1;
        if (!succeeded(m_device.factory()->CreateSwapChainForHwnd(m_device.queue(), hwnd, &scDesc, nullptr, nullptr,
                                                                  &swapchain1),
                       "CreateSwapChainForHwnd"))
            return false;
        if (!succeeded(swapchain1.As(&m_swapchain), "QueryInterface(IDXGISwapChain3)"))
            return false;

        // SDL owns the window and its fullscreen handling; DXGI's Alt+Enter would fight it.
        m_device.factory()->MakeWindowAssociation(hwnd, DXGI_MWA_NO_ALT_ENTER);

        // With the waitable object, DXGI tells the CPU when to start a frame instead of blocking
        // inside Present(), which keeps input-to-display latency to at most this many frames.
        m_swapchain->SetMaximumFrameLatency(kMaxFramesInFlight);
        m_frameLatencyWaitable = m_swapchain->GetFrameLatencyWaitableObject();

        D3D12_DESCRIPTOR_HEAP_DESC heapDesc{};
        heapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
        heapDesc.NumDescriptors = kBufferCount;
        if (!succeeded(m_device.native()->CreateDescriptorHeap(&heapDesc, IID_PPV_ARGS(&m_rtvHeap)),
                       "CreateDescriptorHeap(RTV)"))
            return false;
        setDebugName(m_rtvHeap.Get(), "Swapchain RTV Heap");

        if (!createBackbufferViews())
            return false;

        ARC_LOG_INFO("D3D12 swapchain: {}x{} px, {} buffers, tearing {}", desc.size.width, desc.size.height,
                     kBufferCount, m_device.tearingSupported() ? "supported" : "unsupported");
        return true;
    }

    bool D3D12Swapchain::createBackbufferViews()
    {
        ID3D12Device* device = m_device.native();
        const UINT rtvStride = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
        D3D12_CPU_DESCRIPTOR_HANDLE rtv = m_rtvHeap->GetCPUDescriptorHandleForHeapStart();

        for (uint32_t i = 0; i < kBufferCount; ++i)
        {
            ComPtr<ID3D12Resource> buffer;
            if (!succeeded(m_swapchain->GetBuffer(i, IID_PPV_ARGS(&buffer)), "IDXGISwapChain::GetBuffer"))
                return false;
            setDebugName(buffer.Get(), std::format("Backbuffer {}", i));

            device->CreateRenderTargetView(buffer.Get(), nullptr, rtv);

            D3D12Texture& texture = m_backbuffers[i];
            texture.reset(std::move(buffer), {m_desc.size, m_desc.format}, D3D12_RESOURCE_STATE_PRESENT);
            texture.rtv = rtv;
            rtv.ptr += rtvStride;
        }
        return true;
    }

    void D3D12Swapchain::releaseBackbuffers()
    {
        for (D3D12Texture& texture : m_backbuffers)
            texture.release();
        m_acquired = nullptr;
    }

    bool D3D12Swapchain::resize(Extent2D size)
    {
        // ResizeBuffers fails while any reference to a backbuffer exists, including ones held by
        // command lists the GPU has not finished.
        m_device.waitIdle();
        releaseBackbuffers();

        if (!succeeded(m_swapchain->ResizeBuffers(kBufferCount, size.width, size.height, toDxgiFormat(m_desc.format),
                                                  swapchainFlags()),
                       "IDXGISwapChain::ResizeBuffers"))
            return false;

        m_desc.size = size;
        return createBackbufferViews();
    }

    void D3D12Swapchain::waitUntilReady()
    {
        // The timeout guards against a hang if the display stops presenting (e.g. a lost output).
        if (m_frameLatencyWaitable)
            WaitForSingleObjectEx(m_frameLatencyWaitable, 1000, TRUE);
    }

    Texture* D3D12Swapchain::acquireBackbuffer()
    {
        if (!m_acquired)
            m_acquired = &m_backbuffers[m_swapchain->GetCurrentBackBufferIndex()];
        return m_acquired;
    }

    bool D3D12Swapchain::present()
    {
        // Tearing is only allowed with a sync interval of 0, and makes "vsync off" actually
        // uncapped on windowed flip-model swapchains.
        const UINT syncInterval = m_desc.vsync ? 1 : 0;
        const UINT flags = (!m_desc.vsync && m_device.tearingSupported()) ? DXGI_PRESENT_ALLOW_TEARING : 0;
        m_acquired = nullptr;

        const HRESULT hr = m_swapchain->Present(syncInterval, flags);
        if (hr == DXGI_ERROR_DEVICE_REMOVED || hr == DXGI_ERROR_DEVICE_RESET)
            return succeeded(hr, "IDXGISwapChain::Present");
        return true;
    }
}

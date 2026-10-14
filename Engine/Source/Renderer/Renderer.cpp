#include "Renderer/Renderer.h"

#include "Core/Assert.h"
#include "Core/Log.h"
#include "Platform/Window.h"
#include "RHI/CommandList.h"
#include "RHI/Device.h"
#include "RHI/Swapchain.h"

namespace arc
{
    Renderer::Renderer() = default;

    Renderer::~Renderer()
    {
        shutdown();
    }

    bool Renderer::init(Window& window, const rhi::DeviceDesc& deviceDesc, bool vsync)
    {
        m_device = rhi::Device::create(deviceDesc);
        if (!m_device)
            return false;

        const rhi::AdapterInfo& adapter = m_device->adapterInfo();
        ARC_LOG_INFO("GPU: {} ({}, {} MB{})", adapter.name, rhi::backendName(adapter.backend),
                     adapter.videoMemoryMB, adapter.unifiedMemory ? ", unified" : "");

        rhi::SwapchainDesc swapchainDesc;
        swapchainDesc.size = window.pixelSize();
        swapchainDesc.vsync = vsync;
        m_swapchain = m_device->createSwapchain(window, swapchainDesc);
        return m_swapchain != nullptr;
    }

    void Renderer::shutdown()
    {
        ARC_ASSERT(!isFrameActive(), "Renderer shut down in the middle of a frame");
        if (m_device)
            m_device->waitIdle();
        m_swapchain.reset();
        m_device.reset();
    }

    void Renderer::waitIdle()
    {
        if (m_device)
            m_device->waitIdle();
    }

    void Renderer::waitUntilReady()
    {
        m_swapchain->waitUntilReady();
    }

    bool Renderer::beginFrame()
    {
        ARC_ASSERT(!isFrameActive());

        if (m_pendingSize)
        {
            const Extent2D size = *m_pendingSize;
            const Extent2D current = m_swapchain->desc().size;
            if (size.width > 0 && size.height > 0 && (size.width != current.width || size.height != current.height))
            {
                if (!m_swapchain->resize(size))
                    return false;
                ARC_LOG_TRACE("Swapchain resized to {}x{} px", size.width, size.height);
            }
            m_pendingSize.reset();
        }

        const Extent2D size = m_swapchain->desc().size;
        if (size.width == 0 || size.height == 0)
            return false;

        m_commandList = &m_device->beginFrame(*m_swapchain);
        m_backbufferCleared = false;
        return true;
    }

    void Renderer::endFrame()
    {
        ARC_ASSERT(isFrameActive());

        if (!m_backbufferCleared && beginBackbufferPass("Clear"))
            endBackbufferPass();

        m_device->endFrame(*m_swapchain);
        m_commandList = nullptr;
    }

    rhi::CommandList& Renderer::commandList()
    {
        ARC_ASSERT(isFrameActive(), "No frame is being recorded");
        return *m_commandList;
    }

    bool Renderer::beginBackbufferPass(std::string_view debugName)
    {
        rhi::Texture* backbuffer = m_swapchain->acquireBackbuffer();
        if (!backbuffer)
            return false;

        rhi::RenderPassDesc pass;
        pass.color.texture = backbuffer;
        pass.color.load = m_backbufferCleared ? rhi::LoadOp::Load : rhi::LoadOp::Clear;
        pass.color.clearColor = m_clearColor;
        pass.debugName = debugName;
        commandList().beginRenderPass(pass);

        m_backbufferCleared = true;
        return true;
    }

    void Renderer::endBackbufferPass()
    {
        commandList().endRenderPass();
    }

    void Renderer::setVSync(bool enabled)
    {
        m_swapchain->setVSync(enabled);
    }

    bool Renderer::vsync() const
    {
        return m_swapchain->desc().vsync;
    }
}

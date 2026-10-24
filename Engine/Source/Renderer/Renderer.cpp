#include "Renderer/Renderer.h"

#include "Core/Assert.h"
#include "Core/Log.h"
#include "Platform/Window.h"
#include "RHI/BufferUtils.h"
#include "RHI/CommandList.h"
#include "RHI/Device.h"
#include "RHI/Swapchain.h"
#include "RHI/Texture.h"
#include "RHI/TextureUtils.h"
#include "Renderer/GeometryPool.h"
#include "Renderer/ShaderLibrary.h"
#include "Renderer/ToneMapper.h"

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

#if ARC_DEBUG
        // A failure here means resource creation, copies or barriers are broken on this backend;
        // the editor still starts so the log can be read.
        if (rhi::verifyBufferRoundTrip(*m_device))
            ARC_LOG_INFO("RHI self-test: buffer upload and readback round-trip passed");
        else
            ARC_LOG_ERROR("RHI self-test: buffer upload and readback round-trip FAILED");

        if (rhi::verifyTextureRoundTrip(*m_device))
            ARC_LOG_INFO("RHI self-test: texture upload and readback round-trip passed");
        else
            ARC_LOG_ERROR("RHI self-test: texture upload and readback round-trip FAILED");
#endif

        m_shaders = std::make_unique<ShaderLibrary>(*m_device);
        m_geometry = std::make_unique<GeometryPool>(*m_device);
        ARC_LOG_INFO("Shaders are loaded from {}", m_shaders->directory());
        m_toneMapper = std::make_unique<ToneMapper>();
        if (!m_toneMapper->init(*m_device, *m_shaders, displayFormat()))
            return false;

        rhi::SwapchainDesc swapchainDesc;
        swapchainDesc.size = window.pixelSize();
        swapchainDesc.vsync = vsync;
        m_swapchain = m_device->createSwapchain(window, swapchainDesc);
        if (!m_swapchain)
            return false;

        // The Scene panel resizes them to fit as soon as it is drawn.
        setSceneSize(swapchainDesc.size);
        return m_sceneTarget != nullptr && m_sceneDepth != nullptr && m_displayTarget != nullptr;
    }

    void Renderer::shutdown()
    {
        ARC_ASSERT(!isFrameActive(), "Renderer shut down in the middle of a frame");
        if (m_device)
            m_device->waitIdle();
        m_toneMapper.reset();
        m_shaders.reset();
        m_geometry.reset();
        m_sceneTarget.reset();
        m_sceneDepth.reset();
        m_displayTarget.reset();
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
        // beginFrame() has waited for the oldest frame in flight, so its freed ranges can be reused.
        m_geometry->collect(m_device->frameNumber());
        m_backbufferCleared = false;
        m_sceneCleared = false;
        m_sceneResolved = false;
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

    bool Renderer::beginScenePass(std::string_view debugName)
    {
        ARC_ASSERT(!m_sceneResolved, "Scene pass '{}' begun after the scene was tone mapped", debugName);
        if (!m_sceneTarget || !m_sceneDepth)
            return false;

        rhi::RenderPassDesc pass;
        pass.color.texture = m_sceneTarget.get();
        pass.color.load = m_sceneCleared ? rhi::LoadOp::Load : rhi::LoadOp::Clear;
        pass.color.clearColor = m_sceneClearColor;
        pass.depth.texture = m_sceneDepth.get();
        pass.depth.load = m_sceneCleared ? rhi::LoadOp::Load : rhi::LoadOp::Clear;
        pass.depth.clearDepth = m_sceneCamera.clearDepth();
        pass.debugName = debugName;
        commandList().beginRenderPass(pass);

        m_sceneCleared = true;
        return true;
    }

    void Renderer::endScenePass()
    {
        commandList().endRenderPass();
    }

    void Renderer::resolveScene()
    {
        if (m_sceneResolved || !m_sceneTarget || !m_displayTarget)
            return;
        if (!m_sceneCleared && beginScenePass("Clear Scene"))
            endScenePass();

        rhi::CommandList& list = commandList();
        list.barrier(*m_sceneTarget, rhi::ResourceState::ShaderRead);
        rhi::RenderPassDesc pass;
        pass.color.texture = m_displayTarget.get();
        // Every pixel is written.
        pass.color.load = rhi::LoadOp::DontCare;
        pass.debugName = "Tone Map";
        list.beginRenderPass(pass);
        m_toneMapper->draw(list, *m_sceneTarget);
        list.endRenderPass();
        m_sceneResolved = true;
    }

    bool Renderer::beginOverlayPass(std::string_view debugName)
    {
        if (!m_displayTarget || !m_sceneDepth)
            return false;
        resolveScene();

        rhi::RenderPassDesc pass;
        pass.color.texture = m_displayTarget.get();
        pass.color.load = rhi::LoadOp::Load;
        pass.depth.texture = m_sceneDepth.get();
        pass.depth.load = rhi::LoadOp::Load;
        pass.debugName = debugName;
        commandList().beginRenderPass(pass);
        return true;
    }

    void Renderer::endOverlayPass()
    {
        commandList().endRenderPass();
    }

    void Renderer::finishScene()
    {
        if (!m_displayTarget)
            return;
        resolveScene();
        commandList().barrier(*m_displayTarget, rhi::ResourceState::ShaderRead);
    }

    void Renderer::setSceneSize(Extent2D pixelSize)
    {
        ARC_ASSERT(!isFrameActive(), "The scene target cannot be resized while a frame is being recorded");
        if (pixelSize.width == 0 || pixelSize.height == 0)
            return;
        if (m_displayTarget && m_displayTarget->desc().size.width == pixelSize.width &&
            m_displayTarget->desc().size.height == pixelSize.height)
            return;

        // Frames in flight may still read the old targets; the device keeps them until they finish.
        m_sceneTarget = m_device->createTexture({
            .size = pixelSize,
            .format = sceneFormat(),
            .usage = rhi::TextureUsage::RenderTarget | rhi::TextureUsage::Sampled,
            .debugName = "Scene HDR",
        });
        m_sceneDepth = m_device->createTexture({
            .size = pixelSize,
            .format = sceneDepthFormat(),
            .usage = rhi::TextureUsage::DepthStencil,
            .debugName = "Scene Depth",
        });
        m_displayTarget = m_device->createTexture({
            .size = pixelSize,
            .format = displayFormat(),
            .usage = rhi::TextureUsage::RenderTarget | rhi::TextureUsage::Sampled,
            .debugName = "Scene Display",
        });
    }

    ViewConstants Renderer::sceneViewConstants() const
    {
        const Extent2D size = sceneSize();
        const float aspect = size.height > 0 ? static_cast<float>(size.width) / static_cast<float>(size.height) : 1.0f;

        ViewConstants constants;
        constants.viewProjection = m_sceneCamera.projection(aspect) * m_sceneCamera.view();
        constants.inverseViewProjection = inverse(constants.viewProjection);
        constants.cameraPosition = m_sceneCamera.position;
        return constants;
    }

    Extent2D Renderer::sceneSize() const
    {
        return m_sceneTarget ? m_sceneTarget->desc().size : Extent2D{};
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

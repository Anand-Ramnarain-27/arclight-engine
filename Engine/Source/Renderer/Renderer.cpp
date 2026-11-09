#include "Renderer/Renderer.h"

#include "Core/Assert.h"
#include "Core/Log.h"
#include "Core/Profiler.h"
#include "Platform/Window.h"
#include "RHI/Buffer.h"
#include "RHI/BufferUtils.h"
#include "RHI/CommandList.h"
#include "RHI/Device.h"
#include "RHI/Pipeline.h"
#include "RHI/Shader.h"
#include "RHI/Swapchain.h"
#include "RHI/Texture.h"
#include "RHI/TextureUtils.h"
#include "Renderer/AutoExposure.h"
#include "Renderer/GeometryPool.h"
#include "Renderer/ShaderLibrary.h"
#include "Renderer/ToneMapper.h"

#include <cstddef>
#include <cstring>
#include <vector>

namespace arc
{
#if ARC_DEBUG
    namespace
    {
        // Mirrors SelfTestConstants in ComputeSelfTest.hlsl.
        struct SelfTestConstants
        {
            uint32_t multiplier = 0;
            uint32_t padding[3]{};
        };

        // Runs ComputeSelfTest.hlsl on the GPU and checks what it wrote: storage buffer and texture
        // writes, constants, group-shared memory, group barriers and wave intrinsics.
        bool verifyCompute(rhi::Device& device, const ShaderLibrary& shaders)
        {
            constexpr uint32_t kSize = 16;
            constexpr uint32_t kGroupSize = 8;
            constexpr uint32_t kGroups = (kSize / kGroupSize) * (kSize / kGroupSize);
            constexpr uint32_t kMultiplier = 3;

            const auto shader = shaders.load("ComputeSelfTest", rhi::ShaderStage::Compute, "CSMain");
            if (!shader)
                return false;
            const auto pipeline = device.createComputePipeline({
                .computeShader = shader.get(),
                .threadGroupSize = {kGroupSize, kGroupSize, 1},
                .debugName = "Compute Self-Test",
            });
            const auto values = device.createBuffer({
                .size = (kSize * kSize + kGroups) * sizeof(uint32_t),
                .usage = rhi::BufferUsage::Storage,
                .stride = sizeof(uint32_t),
                .debugName = "Compute Self-Test Values",
            });
            const auto image = device.createTexture({
                .size = {kSize, kSize},
                .format = rhi::Format::RGBA8_UNORM,
                .usage = rhi::TextureUsage::Storage,
                .debugName = "Compute Self-Test Image",
            });
            if (!pipeline || !values || !image)
                return false;

            device.submitImmediate([&](rhi::CommandList& list) {
                list.barrier(*values, rhi::ResourceState::UnorderedAccess);
                list.barrier(*image, rhi::ResourceState::UnorderedAccess);
                list.beginComputePass({.debugName = "Compute Self-Test"});
                list.setComputePipeline(*pipeline);
                list.setStorageBuffer(0, *values);
                list.setStorageTexture(1, *image);
                list.setConstants(0, SelfTestConstants{.multiplier = kMultiplier});
                list.dispatch(kSize / kGroupSize, kSize / kGroupSize);
                list.endComputePass();
            });

            const std::vector<std::byte> bytes = rhi::readbackBuffer(device, *values, 0, values->desc().size);
            const std::vector<std::byte> pixels = rhi::readbackTexture(device, *image, 0);
            if (bytes.size() != values->desc().size || pixels.size() != kSize * kSize * 4)
                return false;

            std::vector<uint32_t> written(bytes.size() / sizeof(uint32_t));
            std::memcpy(written.data(), bytes.data(), bytes.size());
            for (uint32_t i = 0; i < kSize * kSize; ++i)
                if (written[i] != i * kMultiplier + 1)
                {
                    ARC_LOG_ERROR("Compute self-test: value {} is {}, expected {}", i, written[i], i * kMultiplier + 1);
                    return false;
                }
            for (uint32_t group = 0; group < kGroups; ++group)
                if (written[kSize * kSize + group] != kGroupSize * kGroupSize)
                {
                    ARC_LOG_ERROR("Compute self-test: group {} counted {} threads, expected {}", group,
                                  written[kSize * kSize + group], kGroupSize * kGroupSize);
                    return false;
                }
            // x / 15 stored as UNORM is exactly 17x.
            for (uint32_t y = 0; y < kSize; ++y)
                for (uint32_t x = 0; x < kSize; ++x)
                {
                    const auto* pixel = pixels.data() + (y * kSize + x) * 4;
                    if (std::to_integer<uint32_t>(pixel[0]) != 17 * x || std::to_integer<uint32_t>(pixel[1]) != 17 * y ||
                        std::to_integer<uint32_t>(pixel[3]) != 255)
                    {
                        ARC_LOG_ERROR("Compute self-test: pixel ({}, {}) is wrong", x, y);
                        return false;
                    }
                }
            return true;
        }
    }
#endif

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
        m_autoExposure = std::make_unique<AutoExposure>();
        if (!m_autoExposure->init(*m_device, *m_shaders))
            return false;

#if ARC_DEBUG
        // The Null backend records compute work but runs none of it.
        if (adapter.backend != rhi::Backend::Null)
        {
            if (verifyCompute(*m_device, *m_shaders))
                ARC_LOG_INFO("RHI self-test: compute (storage buffer and texture, group-shared memory, waves of {}) passed",
                             m_device->adapterInfo().waveLaneCountMin);
            else
                ARC_LOG_ERROR("RHI self-test: compute FAILED");
        }
#endif

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
        m_autoExposure.reset();
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
        ARC_PROFILE_SCOPE("Wait for swapchain");
        m_swapchain->waitUntilReady();
    }

    bool Renderer::beginFrame()
    {
        ARC_PROFILE_SCOPE("Begin GPU frame");
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

        {
            // Blocks until the GPU has finished the frame that last used this frame slot.
            ARC_PROFILE_SCOPE("Wait for GPU");
            m_commandList = &m_device->beginFrame(*m_swapchain);
        }
        m_autoExposure->collectReadback(m_device->frameSlot());
        const rhi::GpuFrameTimings& gpu = m_device->gpuTimings();
        if (gpu.valid && gpu.frameNumber != m_plottedGpuFrame)
        {
            profiler::plot("GPU frame (ms)", gpu.frameMs);
            m_plottedGpuFrame = gpu.frameNumber;
        }
        // beginFrame() has waited for the oldest frame in flight, so its freed ranges can be reused.
        m_geometry->collect(m_device->frameNumber());
        m_backbufferCleared = false;
        m_sceneCleared = false;
        m_sceneResolved = false;
        return true;
    }

    void Renderer::endFrame()
    {
        ARC_PROFILE_SCOPE("Submit and present");
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

        ARC_PROFILE_SCOPE("Tone map");
        rhi::CommandList& list = commandList();
        list.barrier(*m_sceneTarget, rhi::ResourceState::ShaderRead);
        m_autoExposure->record(list, *m_sceneTarget, m_device->frameSlot());
        rhi::RenderPassDesc pass;
        pass.color.texture = m_displayTarget.get();
        // Every pixel is written.
        pass.color.load = rhi::LoadOp::DontCare;
        pass.debugName = "Tone Map";
        list.beginRenderPass(pass);
        m_toneMapper->draw(list, *m_sceneTarget, *m_autoExposure);
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

#pragma once

// Objective-C++ only: the classes below hold Objective-C object pointers, which are managed
// by ARC (-fobjc-arc), so member layout differs between Objective-C++ and plain C++.
#ifndef __OBJC__
    #error "MetalBackend.h can only be included from Objective-C++ (.mm) files."
#endif

#include "RHI/CommandList.h"
#include "RHI/Device.h"
#include "RHI/Swapchain.h"
#include "RHI/Texture.h"

#import <Metal/Metal.h>
#import <QuartzCore/CAMetalLayer.h>

#include <SDL3/SDL_metal.h>

#include <vector>

namespace arc::rhi::metal
{
    class MetalDevice;

    [[nodiscard]] MTLPixelFormat toMTLPixelFormat(Format format);

    class MetalTexture final : public Texture
    {
    public:
        void reset(id<MTLTexture> texture, const TextureDesc& desc)
        {
            m_texture = texture;
            m_desc = desc;
        }

        [[nodiscard]] id<MTLTexture> native() const { return m_texture; }

    private:
        id<MTLTexture> m_texture = nil;
    };

    class MetalCommandList final : public CommandList
    {
    public:
        void begin(id<MTLCommandBuffer> commandBuffer);

        /// Returns the command buffer, ready to commit, and resets the list.
        [[nodiscard]] id<MTLCommandBuffer> finish();

        void beginRenderPass(const RenderPassDesc& desc) override;
        void endRenderPass() override;
        void pushDebugGroup(std::string_view name) override;
        void popDebugGroup() override;

        [[nodiscard]] id<MTLCommandBuffer> commandBuffer() const { return m_commandBuffer; }

        /// Only valid inside a render pass.
        [[nodiscard]] id<MTLRenderCommandEncoder> renderEncoder() const { return m_renderEncoder; }
        [[nodiscard]] MTLRenderPassDescriptor* renderPassDescriptor() const { return m_renderPassDescriptor; }

    private:
        id<MTLCommandBuffer> m_commandBuffer = nil;
        id<MTLRenderCommandEncoder> m_renderEncoder = nil;
        MTLRenderPassDescriptor* m_renderPassDescriptor = nil;

        // Debug groups pushed outside a pass go on the command buffer, inside a pass on the
        // encoder. true = pushed on the encoder.
        std::vector<bool> m_debugGroupOnEncoder;
    };

    class MetalSwapchain final : public Swapchain
    {
    public:
        explicit MetalSwapchain(MetalDevice& device) : m_device(device) {}
        ~MetalSwapchain() override;

        [[nodiscard]] bool init(Window& window, const SwapchainDesc& desc);

        [[nodiscard]] bool resize(Extent2D size) override;
        [[nodiscard]] Texture* acquireBackbuffer() override;
        void setVSync(bool enabled) override;

        /// The drawable acquired this frame, or nil. Cleared by takeDrawable().
        [[nodiscard]] id<CAMetalDrawable> takeDrawable();

    private:
        MetalDevice& m_device;
        SDL_MetalView m_view = nullptr;
        CAMetalLayer* m_layer = nil;
        id<CAMetalDrawable> m_drawable = nil;
        MetalTexture m_backbuffer;
    };

    class MetalDevice final : public Device
    {
    public:
        MetalDevice() = default;
        ~MetalDevice() override;

        [[nodiscard]] bool init(const DeviceDesc& desc);

        [[nodiscard]] const AdapterInfo& adapterInfo() const override { return m_adapterInfo; }
        [[nodiscard]] std::unique_ptr<Swapchain> createSwapchain(Window& window, const SwapchainDesc& desc) override;
        [[nodiscard]] CommandList& beginFrame(Swapchain& swapchain) override;
        void endFrame(Swapchain& swapchain) override;
        void waitIdle() override;

        [[nodiscard]] id<MTLDevice> native() const { return m_device; }
        [[nodiscard]] id<MTLCommandQueue> queue() const { return m_queue; }

    private:
        id<MTLDevice> m_device = nil;
        id<MTLCommandQueue> m_queue = nil;

        // Counts free frame slots. beginFrame() takes one; the command buffer's completion
        // handler gives it back when the GPU has finished that frame.
        dispatch_semaphore_t m_frameSemaphore = nil;

        id<MTLCommandBuffer> m_lastSubmitted = nil;
        void* m_frameAutoreleasePool = nullptr;
        MetalCommandList m_commandList;
        AdapterInfo m_adapterInfo;
    };
}

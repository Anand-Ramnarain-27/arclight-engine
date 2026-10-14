#pragma once

// Objective-C++ only: the classes below hold Objective-C object pointers, which are managed
// by ARC (-fobjc-arc), so member layout differs between Objective-C++ and plain C++.
#ifndef __OBJC__
    #error "MetalBackend.h can only be included from Objective-C++ (.mm) files."
#endif

#include "RHI/Buffer.h"
#include "RHI/CommandList.h"
#include "RHI/Device.h"
#include "RHI/Pipeline.h"
#include "RHI/Shader.h"
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

    // SPIRV-Cross assigns [[buffer(n)]] indices to constant and structured buffers from 0
    // upwards, so the vertex buffer goes in the last of Metal's 31 buffer slots to stay clear.
    inline constexpr NSUInteger kVertexBufferIndex = 30;

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

    class MetalBuffer final : public Buffer
    {
    public:
        MetalBuffer(id<MTLBuffer> buffer, const BufferDesc& desc, ResourceState state) : m_buffer(buffer)
        {
            m_desc = desc;
            m_mapped = desc.memory == MemoryType::GpuOnly ? nullptr : buffer.contents;
            m_state = state;
        }

        [[nodiscard]] id<MTLBuffer> native() const { return m_buffer; }

    private:
        id<MTLBuffer> m_buffer = nil;
    };

    class MetalShader final : public Shader
    {
    public:
        MetalShader(const ShaderDesc& desc, id<MTLLibrary> library, id<MTLFunction> function)
            : Shader(desc)
            , m_library(library)
            , m_function(function)
        {
        }

        [[nodiscard]] id<MTLLibrary> library() const { return m_library; }
        [[nodiscard]] id<MTLFunction> function() const { return m_function; }

    private:
        id<MTLLibrary> m_library = nil;
        id<MTLFunction> m_function = nil;
    };

    class MetalPipeline final : public Pipeline
    {
    public:
        MetalPipeline(const PipelineDesc& desc, id<MTLRenderPipelineState> state) : Pipeline(desc), m_state(state) {}

        [[nodiscard]] id<MTLRenderPipelineState> state() const { return m_state; }

    private:
        id<MTLRenderPipelineState> m_state = nil;
    };

    class MetalCommandList final : public CommandList
    {
    public:
        void begin(id<MTLCommandBuffer> commandBuffer);

        /// Returns the command buffer, ready to commit, and resets the list.
        [[nodiscard]] id<MTLCommandBuffer> finish();

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

        [[nodiscard]] id<MTLCommandBuffer> commandBuffer() const { return m_commandBuffer; }

        /// Only valid inside a render pass.
        [[nodiscard]] id<MTLRenderCommandEncoder> renderEncoder() const { return m_renderEncoder; }
        [[nodiscard]] MTLRenderPassDescriptor* renderPassDescriptor() const { return m_renderPassDescriptor; }

    private:
        // Copies need a blit encoder. Consecutive copies share one; it is ended before a render
        // pass, a debug group change, or the end of the command buffer.
        id<MTLBlitCommandEncoder> blitEncoder();
        void endBlitEncoder();

        id<MTLCommandBuffer> m_commandBuffer = nil;
        id<MTLRenderCommandEncoder> m_renderEncoder = nil;
        id<MTLBlitCommandEncoder> m_blitEncoder = nil;

        // Metal takes the index buffer as a draw argument rather than as bound state.
        id<MTLBuffer> m_indexBuffer = nil;
        NSUInteger m_indexBufferOffset = 0;
        MTLIndexType m_indexType = MTLIndexTypeUInt16;
        bool m_hasPipeline = false;
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
        [[nodiscard]] std::unique_ptr<Buffer> createBuffer(const BufferDesc& desc) override;
        [[nodiscard]] ShaderFormat shaderFormat() const override { return ShaderFormat::MetalLib; }
        [[nodiscard]] std::unique_ptr<Shader> createShader(const ShaderDesc& desc) override;
        [[nodiscard]] std::unique_ptr<Pipeline> createPipeline(const PipelineDesc& desc) override;
        void submitImmediate(const std::function<void(CommandList&)>& record) override;
        [[nodiscard]] std::unique_ptr<Swapchain> createSwapchain(Window& window, const SwapchainDesc& desc) override;
        [[nodiscard]] CommandList& beginFrame(Swapchain& swapchain) override;
        void endFrame(Swapchain& swapchain) override;
        void waitIdle() override;

        [[nodiscard]] std::string_view gpuCaptureTool() const override;
        void captureNextFrame() override { m_captureRequested = true; }

        [[nodiscard]] id<MTLDevice> native() const { return m_device; }
        [[nodiscard]] id<MTLCommandQueue> queue() const { return m_queue; }

    private:
        void startCapture();
        id<MTLDevice> m_device = nil;
        id<MTLCommandQueue> m_queue = nil;

        // Counts free frame slots. beginFrame() takes one; the command buffer's completion
        // handler gives it back when the GPU has finished that frame.
        dispatch_semaphore_t m_frameSemaphore = nil;

        id<MTLCommandBuffer> m_lastSubmitted = nil;
        void* m_frameAutoreleasePool = nullptr;
        bool m_captureRequested = false;
        bool m_capturing = false;
        MetalCommandList m_commandList;
        AdapterInfo m_adapterInfo;
    };
}

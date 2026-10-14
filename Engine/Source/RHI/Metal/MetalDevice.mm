#include "RHI/Metal/MetalBackend.h"

#include "Core/Assert.h"
#include "Core/Log.h"
#include "RHI/Backends.h"

#import <Foundation/Foundation.h>

// The Objective-C runtime functions behind @autoreleasepool. A frame spans beginFrame() and
// endFrame(), which a lexical @autoreleasepool block cannot, so the pool is pushed and popped
// explicitly. Declared in <objc/objc-internal.h>, which is not a public header.
extern "C" void* objc_autoreleasePoolPush(void);
extern "C" void objc_autoreleasePoolPop(void* pool);

namespace arc::rhi::metal
{
    MTLPixelFormat toMTLPixelFormat(Format format)
    {
        switch (format)
        {
            case Format::Unknown:     return MTLPixelFormatInvalid;
            case Format::RGBA8_UNORM: return MTLPixelFormatRGBA8Unorm;
            case Format::BGRA8_UNORM: return MTLPixelFormatBGRA8Unorm;
        }
        return MTLPixelFormatInvalid;
    }

    MetalDevice::~MetalDevice()
    {
        waitIdle();

        // libdispatch aborts if a semaphore is released while its count is below the initial
        // value. Taking every slot guarantees all completion handlers have run; the slots are
        // then returned so the count matches the initial value again.
        if (m_frameSemaphore)
        {
            for (uint32_t i = 0; i < kMaxFramesInFlight; ++i)
                dispatch_semaphore_wait(m_frameSemaphore, DISPATCH_TIME_FOREVER);
            for (uint32_t i = 0; i < kMaxFramesInFlight; ++i)
                dispatch_semaphore_signal(m_frameSemaphore);
        }
    }

    bool MetalDevice::init(const DeviceDesc& /*desc*/)
    {
        // On a Mac with several GPUs this is the one driving the main display, which suits a
        // windowed app: presenting from another GPU would add a cross-GPU copy every frame.
        m_device = MTLCreateSystemDefaultDevice();
        if (!m_device)
        {
            ARC_LOG_ERROR("No Metal device available");
            return false;
        }

        if (![m_device supportsFamily:MTLGPUFamilyMetal3])
        {
            ARC_LOG_ERROR("'{}' does not support the Metal 3 GPU family", m_device.name.UTF8String);
            return false;
        }

        m_queue = [m_device newCommandQueue];
        if (!m_queue)
        {
            ARC_LOG_ERROR("Failed to create the Metal command queue");
            return false;
        }
        m_queue.label = @"Main Queue";

        m_frameSemaphore = dispatch_semaphore_create(kMaxFramesInFlight);

        m_adapterInfo.backend = Backend::Metal;
        m_adapterInfo.name = m_device.name.UTF8String;
        m_adapterInfo.unifiedMemory = m_device.hasUnifiedMemory;
        // Metal has no fixed VRAM figure; this is the memory budget Metal recommends for the device.
        m_adapterInfo.videoMemoryMB = m_device.recommendedMaxWorkingSetSize / (1024 * 1024);
        // Metal drivers ship with the OS, so the OS version identifies the driver.
        m_adapterInfo.driver = std::string("macOS ") + NSProcessInfo.processInfo.operatingSystemVersionString.UTF8String;

        ARC_LOG_INFO("Metal API validation is controlled by the Xcode scheme, or MTL_DEBUG_LAYER=1 outside Xcode");
        return true;
    }

    std::unique_ptr<Buffer> MetalDevice::createBuffer(const BufferDesc& desc)
    {
        ARC_ASSERT(desc.size > 0, "Buffer '{}' has zero size", desc.debugName);

        MTLResourceOptions options = MTLResourceHazardTrackingModeDefault;
        ResourceState state = ResourceState::Common;
        switch (desc.memory)
        {
            case MemoryType::GpuOnly:
                options |= MTLResourceStorageModePrivate;
                break;
            case MemoryType::Upload:
                // Write-combined: CPU writes skip the cache, so CPU reads from it are very slow.
                options |= MTLResourceStorageModeShared | MTLResourceCPUCacheModeWriteCombined;
                state = ResourceState::CopySource;
                break;
            case MemoryType::Readback:
                options |= MTLResourceStorageModeShared;
                state = ResourceState::CopyDest;
                break;
        }

        id<MTLBuffer> buffer = [m_device newBufferWithLength:desc.size options:options];
        if (!buffer)
        {
            ARC_LOG_ERROR("Failed to create Metal buffer '{}' ({} bytes)", desc.debugName, desc.size);
            return nullptr;
        }
        if (!desc.debugName.empty())
            buffer.label = [NSString stringWithUTF8String:desc.debugName.c_str()];

        return std::make_unique<MetalBuffer>(buffer, desc, state);
    }

    std::unique_ptr<Shader> MetalDevice::createShader(const ShaderDesc& desc)
    {
        // newLibraryWithData takes a dispatch_data_t; DEFAULT copies the bytes, so the caller's
        // buffer may be freed afterwards.
        dispatch_data_t data = dispatch_data_create(desc.bytecode.data(), desc.bytecode.size(), nullptr,
                                                    DISPATCH_DATA_DESTRUCTOR_DEFAULT);
        NSError* error = nil;
        id<MTLLibrary> library = [m_device newLibraryWithData:data error:&error];
        if (!library)
        {
            ARC_LOG_ERROR("Failed to load Metal library '{}': {}", desc.debugName,
                          error ? error.localizedDescription.UTF8String : "unknown error");
            return nullptr;
        }
        library.label = [NSString stringWithUTF8String:desc.debugName.c_str()];

        id<MTLFunction> function = [library newFunctionWithName:[NSString stringWithUTF8String:desc.entryPoint.c_str()]];
        if (!function)
        {
            ARC_LOG_ERROR("Metal library '{}' has no function named '{}'", desc.debugName, desc.entryPoint);
            return nullptr;
        }

        const MTLFunctionType expected = desc.stage == ShaderStage::Vertex ? MTLFunctionTypeVertex : MTLFunctionTypeFragment;
        if (function.functionType != expected)
        {
            ARC_LOG_ERROR("'{}' in '{}' is not a {} function", desc.entryPoint, desc.debugName, shaderStageName(desc.stage));
            return nullptr;
        }

        return std::make_unique<MetalShader>(desc, library, function);
    }

    std::unique_ptr<Pipeline> MetalDevice::createPipeline(const PipelineDesc& desc)
    {
        ARC_ASSERT(desc.vertexShader && desc.vertexShader->stage() == ShaderStage::Vertex);
        ARC_ASSERT(desc.pixelShader && desc.pixelShader->stage() == ShaderStage::Pixel);

        MTLVertexDescriptor* vertexDescriptor = [MTLVertexDescriptor vertexDescriptor];
        for (NSUInteger i = 0; i < desc.vertexLayout.attributes.size(); ++i)
        {
            const VertexAttribute& attribute = desc.vertexLayout.attributes[i];
            vertexDescriptor.attributes[i].format = attribute.format == VertexFormat::Float2 ? MTLVertexFormatFloat2
                                                  : attribute.format == VertexFormat::Float3 ? MTLVertexFormatFloat3
                                                                                             : MTLVertexFormatFloat4;
            vertexDescriptor.attributes[i].offset = attribute.offset;
            vertexDescriptor.attributes[i].bufferIndex = kVertexBufferIndex;
        }
        vertexDescriptor.layouts[kVertexBufferIndex].stride = desc.vertexLayout.stride;
        vertexDescriptor.layouts[kVertexBufferIndex].stepFunction = MTLVertexStepFunctionPerVertex;

        MTLRenderPipelineDescriptor* descriptor = [[MTLRenderPipelineDescriptor alloc] init];
        descriptor.label = [NSString stringWithUTF8String:desc.debugName.c_str()];
        descriptor.vertexFunction = static_cast<const MetalShader*>(desc.vertexShader)->function();
        descriptor.fragmentFunction = static_cast<const MetalShader*>(desc.pixelShader)->function();
        descriptor.vertexDescriptor = vertexDescriptor;
        descriptor.colorAttachments[0].pixelFormat = toMTLPixelFormat(desc.colorFormat);

        NSError* error = nil;
        id<MTLRenderPipelineState> state = [m_device newRenderPipelineStateWithDescriptor:descriptor error:&error];
        if (!state)
        {
            ARC_LOG_ERROR("Failed to create Metal pipeline '{}': {}", desc.debugName,
                          error ? error.localizedDescription.UTF8String : "unknown error");
            return nullptr;
        }
        return std::make_unique<MetalPipeline>(desc, state);
    }

    void MetalDevice::submitImmediate(const std::function<void(CommandList&)>& record)
    {
        @autoreleasepool
        {
            MetalCommandList commandList;
            id<MTLCommandBuffer> commandBuffer = [m_queue commandBuffer];
            commandBuffer.label = @"Immediate";
            commandList.begin(commandBuffer);
            record(commandList);

            commandBuffer = commandList.finish();
            [commandBuffer commit];
            commandList.decayBufferStates();
            [commandBuffer waitUntilCompleted];

            if (commandBuffer.status == MTLCommandBufferStatusError)
                ARC_LOG_ERROR("Immediate Metal command buffer failed: {}",
                              commandBuffer.error.localizedDescription.UTF8String);
        }
    }

    std::unique_ptr<Swapchain> MetalDevice::createSwapchain(Window& window, const SwapchainDesc& desc)
    {
        auto swapchain = std::make_unique<MetalSwapchain>(*this);
        if (!swapchain->init(window, desc))
            return nullptr;
        return swapchain;
    }

    CommandList& MetalDevice::beginFrame(Swapchain& /*swapchain*/)
    {
        dispatch_semaphore_wait(m_frameSemaphore, DISPATCH_TIME_FOREVER);
        m_frameAutoreleasePool = objc_autoreleasePoolPush();

        id<MTLCommandBuffer> commandBuffer = [m_queue commandBuffer];
        commandBuffer.label = [NSString stringWithFormat:@"Frame %llu", static_cast<unsigned long long>(frameNumber())];
        m_commandList.begin(commandBuffer);
        return m_commandList;
    }

    void MetalDevice::endFrame(Swapchain& swapchain)
    {
        id<MTLCommandBuffer> commandBuffer = m_commandList.finish();
        m_commandList.decayBufferStates();

        if (id<CAMetalDrawable> drawable = static_cast<MetalSwapchain&>(swapchain).takeDrawable())
            [commandBuffer presentDrawable:drawable];

        // The block captures the semaphore, not this, so it stays valid even if the handler
        // runs during device destruction.
        dispatch_semaphore_t semaphore = m_frameSemaphore;
        [commandBuffer addCompletedHandler:^(id<MTLCommandBuffer> completed) {
            if (completed.status == MTLCommandBufferStatusError)
                ARC_LOG_ERROR("Metal command buffer '{}' failed: {}", completed.label.UTF8String,
                              completed.error.localizedDescription.UTF8String);
            dispatch_semaphore_signal(semaphore);
        }];
        [commandBuffer commit];
        m_lastSubmitted = commandBuffer;

        objc_autoreleasePoolPop(m_frameAutoreleasePool);
        m_frameAutoreleasePool = nullptr;
        advanceFrame();
    }

    void MetalDevice::waitIdle()
    {
        // A queue executes command buffers in commit order, so the last one finishing means
        // all of them have.
        [m_lastSubmitted waitUntilCompleted];
        m_lastSubmitted = nil;
    }
}

namespace arc::rhi
{
    std::unique_ptr<Device> createMetalDevice(const DeviceDesc& desc)
    {
        auto device = std::make_unique<metal::MetalDevice>();
        if (!device->init(desc))
            return nullptr;
        return device;
    }
}

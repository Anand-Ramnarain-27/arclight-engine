#include "RHI/Metal/MetalBackend.h"

#include "Core/Log.h"
#include "Platform/Window.h"

namespace arc::rhi::metal
{
    MetalSwapchain::~MetalSwapchain()
    {
        m_device.waitIdle();
        m_drawable = nil;
        m_layer = nil;
        if (m_view)
            SDL_Metal_DestroyView(m_view);
    }

    bool MetalSwapchain::init(Window& window, const SwapchainDesc& desc)
    {
        m_desc = desc;

        m_view = SDL_Metal_CreateView(window.sdlWindow());
        if (!m_view)
        {
            ARC_LOG_ERROR("SDL_Metal_CreateView failed: {}", SDL_GetError());
            return false;
        }
        // __bridge casts have no C++-style equivalent.
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wold-style-cast"
        m_layer = (__bridge CAMetalLayer*)SDL_Metal_GetLayer(m_view);
#pragma clang diagnostic pop

        m_layer.device = m_device.native();
        m_layer.pixelFormat = toMTLPixelFormat(desc.format);
        // Drawables are only ever rendered to, which lets Metal optimise their storage.
        m_layer.framebufferOnly = YES;
        m_layer.maximumDrawableCount = 3;
        m_layer.displaySyncEnabled = desc.vsync ? YES : NO;
        // In pixels. Setting it explicitly avoids depending on the layer's contentsScale.
        m_layer.drawableSize = CGSizeMake(desc.size.width, desc.size.height);

        ARC_LOG_INFO("Metal swapchain: {}x{} px, {} drawables", desc.size.width, desc.size.height,
                     m_layer.maximumDrawableCount);
        return true;
    }

    bool MetalSwapchain::resize(Extent2D size)
    {
        // Drawables already acquired keep their old size; new ones use the new size. No GPU
        // wait is needed, unlike D3D12.
        m_layer.drawableSize = CGSizeMake(size.width, size.height);
        m_desc.size = size;
        return true;
    }

    void MetalSwapchain::setVSync(bool enabled)
    {
        m_desc.vsync = enabled;
        m_layer.displaySyncEnabled = enabled ? YES : NO;
    }

    Texture* MetalSwapchain::acquireBackbuffer()
    {
        if (!m_drawable)
        {
            // Blocks until a drawable is free (up to one second, then returns nil).
            m_drawable = [m_layer nextDrawable];
            if (!m_drawable)
            {
                ARC_LOG_WARN("CAMetalLayer nextDrawable timed out; skipping the frame");
                return nullptr;
            }

            id<MTLTexture> texture = m_drawable.texture;
            m_backbuffer.reset(texture, {.size = {static_cast<uint32_t>(texture.width), static_cast<uint32_t>(texture.height)},
                                         .format = m_desc.format,
                                         .usage = TextureUsage::RenderTarget,
                                         .debugName = "Backbuffer"});
            m_backbuffer.setState(ResourceState::Present);
        }
        return &m_backbuffer;
    }

    id<CAMetalDrawable> MetalSwapchain::takeDrawable()
    {
        id<CAMetalDrawable> drawable = m_drawable;
        m_drawable = nil;
        m_backbuffer.reset(nil, m_backbuffer.desc());
        return drawable;
    }
}

#pragma once

#include "RHI/RHITypes.h"

namespace arc::rhi
{
    class Texture;

    /// The set of images presented to a window. Created by Device::createSwapchain() and
    /// presented by Device::endFrame().
    class Swapchain
    {
    public:
        virtual ~Swapchain() = default;

        Swapchain(const Swapchain&) = delete;
        Swapchain& operator=(const Swapchain&) = delete;

        /// Waits for the GPU to finish with the current images. Must not be called between
        /// Device::beginFrame() and Device::endFrame(). Returns false and logs on failure.
        [[nodiscard]] virtual bool resize(Extent2D size) = 0;

        /// The image to render into this frame. Only valid between Device::beginFrame() and
        /// Device::endFrame(). On Metal this acquires the drawable, which can block, so call it
        /// as late in the frame as possible. Returns nullptr if no image is available.
        [[nodiscard]] virtual Texture* acquireBackbuffer() = 0;

        /// Takes effect at the next present.
        virtual void setVSync(bool enabled) = 0;

        /// Blocks until the presentation engine can accept another frame. Called at the very
        /// start of the frame, before input is read, so the frame uses the freshest input.
        virtual void waitUntilReady() {}

        [[nodiscard]] const SwapchainDesc& desc() const { return m_desc; }

    protected:
        Swapchain() = default;

        SwapchainDesc m_desc;
    };
}

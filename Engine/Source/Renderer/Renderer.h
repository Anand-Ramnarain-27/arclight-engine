#pragma once

#include "RHI/RHITypes.h"

#include <memory>
#include <optional>
#include <string_view>

namespace arc
{
    class Window;

    namespace rhi
    {
        class CommandList;
        class Device;
        class Swapchain;
    }

    /// Owns the GPU device and the window's swapchain, and brackets each frame.
    ///
    /// Owned by the Application, which calls beginFrame() after every module's preRender() and
    /// endFrame() after every module's postRender(). Modules record GPU work in render() and
    /// postRender(), and only while isFrameActive() is true.
    ///
    /// The backbuffer is cleared by whichever pass opens it first in a frame, so passes drawn
    /// later (such as the UI) load what is already there. If nothing draws to it, endFrame()
    /// clears it so a stale image is never presented.
    class Renderer
    {
    public:
        Renderer();
        ~Renderer();

        Renderer(const Renderer&) = delete;
        Renderer& operator=(const Renderer&) = delete;

        /// Returns false and logs the reason on failure.
        [[nodiscard]] bool init(Window& window, const rhi::DeviceDesc& deviceDesc, bool vsync);

        /// Safe to call more than once and after a failed init().
        void shutdown();

        void waitIdle();

        /// The swapchain is resized at the start of the next frame, so several resize events
        /// within one frame cause one resize.
        void onWindowResized(Extent2D pixelSize) { m_pendingSize = pixelSize; }

        /// Called at the start of the main loop iteration, before input is read.
        void waitUntilReady();

        /// Returns false when there is nothing to draw into, such as a zero-sized window.
        [[nodiscard]] bool beginFrame();
        void endFrame();

        [[nodiscard]] bool isFrameActive() const { return m_commandList != nullptr; }

        /// Only valid while isFrameActive() is true.
        [[nodiscard]] rhi::CommandList& commandList();

        /// Opens a render pass on the backbuffer. Returns false if no backbuffer is available
        /// this frame, in which case endBackbufferPass() must not be called.
        [[nodiscard]] bool beginBackbufferPass(std::string_view debugName);
        void endBackbufferPass();

        [[nodiscard]] rhi::Device& device() { return *m_device; }
        [[nodiscard]] const rhi::Device& device() const { return *m_device; }
        [[nodiscard]] rhi::Swapchain& swapchain() { return *m_swapchain; }

        void setVSync(bool enabled);
        [[nodiscard]] bool vsync() const;

        void setClearColor(const rhi::ClearColor& color) { m_clearColor = color; }

    private:
        std::unique_ptr<rhi::Device> m_device;
        std::unique_ptr<rhi::Swapchain> m_swapchain;
        rhi::CommandList* m_commandList = nullptr;
        std::optional<Extent2D> m_pendingSize;
        rhi::ClearColor m_clearColor{0.08f, 0.09f, 0.11f, 1.0f};
        bool m_backbufferCleared = false;
    };
}

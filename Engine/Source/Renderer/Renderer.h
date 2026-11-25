#pragma once

#include "RHI/GpuTiming.h"
#include "RHI/RHITypes.h"
#include "Renderer/Camera.h"
#include "Renderer/ShaderConstants.h"

#include <functional>
#include <memory>
#include <optional>
#include <string_view>
#include <thread>

namespace arc
{
    class AutoExposure;
    class GeometryPool;
    class RenderThread;
    class ShaderLibrary;
    class ToneMapper;
    class Window;

    namespace rhi
    {
        class CommandList;
        class Device;
        class Swapchain;
        class Texture;
    }

    /// Owns the GPU device and the window's swapchain, and brackets each frame.
    ///
    /// Owned by the Application, which calls beginFrame() before every module's render() and
    /// endFrame() after every module's postRender(). Modules record GPU work in render() and
    /// postRender(), and only while isFrameActive() is true.
    ///
    /// Frames are recorded on a render thread of their own, unless that is switched off, while the
    /// main thread updates the next frame. The main thread hands each frame over with submitFrame();
    /// before that, waitForRenderThread() and syncRender() run while the render thread is idle, the
    /// one point at which main-thread state is copied for the frame to draw. So the frame functions
    /// (beginFrame() to endFrame(), the passes, commandList(), frameCamera(), sceneViewConstants())
    /// belong to whichever thread records frames, and the rest to the main thread. Main-thread code
    /// that replaces something a frame draws with calls waitForRenderThread() first, as
    /// setSceneSize() does; immediate GPU submissions wait for the render thread too, since they
    /// change the state of resources that frames use.
    ///
    /// The backbuffer is cleared by whichever pass opens it first in a frame, so passes drawn
    /// later (such as the UI) load what is already there. If nothing draws to it, endFrame()
    /// clears it so a stale image is never presented.
    ///
    /// The scene is not drawn to the backbuffer but in two stages into textures of the same size:
    ///   scene passes    Lit content (meshes, the sky) into the HDR scene target, sceneFormat(), in
    ///                   linear light with no upper limit, with the scene depth target. The first
    ///                   scene pass of a frame clears both.
    ///   overlay passes  Editor visuals (the grid, the selection outline) into the display target,
    ///                   displayFormat(), after the scene has been tone mapped into it, so their
    ///                   colours are shown as written. The scene depth target is loaded for testing.
    /// The tone mapping happens once a frame, when the first overlay pass begins or in finishScene(),
    /// whichever comes first; no scene pass may follow it. finishScene(), called by the Application
    /// after every module's render(), also moves the display target to the ShaderRead state for the
    /// UI, which shows it in the editor's Scene panel.
    ///
    /// The scene is seen through the scene camera, which the editor sets every frame. Pipelines that
    /// test depth must use its depthCompare(); since that changes with Camera::reversedZ, modules
    /// keep a pipeline for each convention.
    class Renderer
    {
    public:
        Renderer();
        ~Renderer();

        Renderer(const Renderer&) = delete;
        Renderer& operator=(const Renderer&) = delete;

        /// Returns false and logs the reason on failure. `renderThread` records frames on a thread of
        /// their own from the first frame.
        [[nodiscard]] bool init(Window& window, const rhi::DeviceDesc& deviceDesc, bool vsync, bool renderThread = true);

        /// Safe to call more than once and after a failed init().
        void shutdown();

        /// Waits for the render thread, then for the GPU.
        void waitIdle();

        /// Switches between recording frames on the render thread and on the main thread, from the
        /// next syncRender().
        void setRenderThreadEnabled(bool enabled) { m_renderThreadWanted = enabled; }
        [[nodiscard]] bool renderThreadEnabled() const { return m_renderThreadWanted; }
        /// Whether the frames now being drawn are recorded on the render thread.
        [[nodiscard]] bool renderThreadRunning() const { return m_renderThread != nullptr; }

        /// Main thread: blocks until the render thread has finished the frame it was given. Until the
        /// next submitFrame(), the main thread may change anything frames use. Returns at once without
        /// a render thread, and on the render thread itself.
        void waitForRenderThread();

        /// Main thread, after waitForRenderThread(): copies what the next frame draws with from the
        /// main thread's settings (the scene camera, tone mapping, auto exposure), resizes the
        /// swapchain, collects what the last frame measured (GPU timings, exposure) and starts or
        /// stops the render thread.
        void syncRender();

        /// Main thread, after syncRender(): records and submits a frame with `work`, on the render
        /// thread, or here when it is off.
        void submitFrame(std::function<void()> work);

        /// Main thread: the GPU timings, and how long the last frame took from beginFrame() to
        /// endFrame(), waits for the GPU and the swapchain included, as of the last syncRender().
        [[nodiscard]] const rhi::GpuFrameTimings& gpuTimings() const { return m_gpuTimings; }
        [[nodiscard]] double recordMilliseconds() const { return m_recordMs; }

        /// The swapchain is resized at the next syncRender(), so several resize events within one
        /// frame cause one resize.
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

        /// Opens a render pass on the HDR scene target and the scene depth. Returns false if there is
        /// none. Not allowed once the scene has been tone mapped this frame.
        [[nodiscard]] bool beginScenePass(std::string_view debugName);
        void endScenePass();

        /// Tone maps the scene if that has not happened yet this frame, then opens a render pass on the
        /// display target with the scene depth loaded. Returns false if there is no display target.
        [[nodiscard]] bool beginOverlayPass(std::string_view debugName);
        void endOverlayPass();

        void finishScene();

        /// Main thread: recreates the scene target if the size differs, first waiting for the render
        /// thread, which may be drawing into the old one. Must be called before anything shows this
        /// frame's scene texture: the old texture is released (deferred until the GPU is done with
        /// it), so an ImTextureID taken from it earlier in the frame would dangle on Metal. Zero sizes
        /// are ignored.
        void setSceneSize(Extent2D pixelSize);
        [[nodiscard]] Extent2D sceneSize() const;

        /// The tone-mapped scene with its overlays, for the UI to show: RenderTarget + Sampled, in
        /// displayFormat(). In the ShaderRead state after finishScene().
        [[nodiscard]] rhi::Texture* displayTexture() { return m_displayTarget.get(); }

        /// The format of the scene's depth target, for pipelines that draw into scene passes.
        [[nodiscard]] static constexpr rhi::Format sceneDepthFormat() { return rhi::Format::D32_FLOAT; }

        /// Main thread. Used by the next frame handed to the render thread.
        void setSceneCamera(const Camera& camera) { m_sceneCamera = camera; }
        [[nodiscard]] const Camera& sceneCamera() const { return m_sceneCamera; }
        /// The camera of the frame being recorded: the scene camera as of the last syncRender().
        [[nodiscard]] const Camera& frameCamera() const { return m_frameCamera; }

        /// The frame camera's constants for b0, with the aspect ratio of the scene target.
        [[nodiscard]] ViewConstants sceneViewConstants() const;

        /// The colour format of the HDR scene target, for pipelines drawn in scene passes. Shaders write
        /// linear radiance to it, unclamped.
        [[nodiscard]] static constexpr rhi::Format sceneFormat() { return rhi::Format::RGBA16_FLOAT; }

        /// The colour format of the display target, for pipelines drawn in overlay passes. Not sRGB:
        /// the tone-mapping pass encodes (LinearToSrgb in Common.hlsli), and overlays write
        /// display-ready values.
        [[nodiscard]] static constexpr rhi::Format displayFormat() { return rhi::Format::RGBA8_UNORM; }

        /// Exposure and the tone curve, applied when the scene is copied into the display target.
        [[nodiscard]] ToneMapper& toneMapper() { return *m_toneMapper; }

        /// Measures the scene's brightness on the GPU each frame while enabled, for the tone mapper.
        [[nodiscard]] AutoExposure& autoExposure() { return *m_autoExposure; }

        [[nodiscard]] rhi::Device& device() { return *m_device; }
        [[nodiscard]] const rhi::Device& device() const { return *m_device; }
        [[nodiscard]] rhi::Swapchain& swapchain() { return *m_swapchain; }
        [[nodiscard]] const ShaderLibrary& shaders() const { return *m_shaders; }
        /// Static geometry for every model. Destroyed with the renderer, so models must be gone first.
        [[nodiscard]] GeometryPool& geometry() { return *m_geometry; }

        /// Waits for the render thread, which presents.
        void setVSync(bool enabled);
        [[nodiscard]] bool vsync() const;

        /// The backbuffer's clear colour, display-referred.
        void setClearColor(const rhi::ClearColor& color) { m_clearColor = color; }

    private:
        /// Tone maps the HDR scene into the display target, once a frame.
        void resolveScene();

        std::unique_ptr<rhi::Device> m_device;
        std::unique_ptr<rhi::Swapchain> m_swapchain;
        std::unique_ptr<ShaderLibrary> m_shaders;
        std::unique_ptr<GeometryPool> m_geometry;
        std::unique_ptr<ToneMapper> m_toneMapper;
        std::unique_ptr<AutoExposure> m_autoExposure;
        rhi::CommandList* m_commandList = nullptr;
        std::optional<Extent2D> m_pendingSize;
        rhi::ClearColor m_clearColor{0.08f, 0.09f, 0.11f, 1.0f};
        bool m_backbufferCleared = false;

        std::unique_ptr<rhi::Texture> m_sceneTarget;
        std::unique_ptr<rhi::Texture> m_sceneDepth;
        std::unique_ptr<rhi::Texture> m_displayTarget;
        // Linear: the backbuffer colour above, decoded from sRGB. Tone curves with a toe (Khronos PBR
        // Neutral) darken it further.
        rhi::ClearColor m_sceneClearColor{0.0072f, 0.0085f, 0.0116f, 1.0f};
        Camera m_sceneCamera;
        Camera m_frameCamera;
        std::unique_ptr<RenderThread> m_renderThread;
        bool m_renderThreadWanted = true;
        std::thread::id m_mainThread;
        rhi::GpuFrameTimings m_gpuTimings;
        double m_recordMs = 0.0;
        bool m_sceneCleared = false;
        bool m_sceneResolved = false;
        uint64_t m_plottedGpuFrame = ~0ull;
    };
}

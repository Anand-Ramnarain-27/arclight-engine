#pragma once

#include "Math/Math.h"

#include <memory>
#include <vector>

namespace arc
{
    class GameObject;
    class Renderer;

    namespace rhi
    {
        class Pipeline;
        class Texture;
    }

    /// Draws an outline around the selected object and everything below it, over the scene.
    ///
    /// Two passes: the selected meshes' silhouettes are drawn into a mask the size of the scene
    /// target (Engine/Shaders/SelectionMask.hlsl), then a full-screen pass over the scene marks the
    /// pixels just outside the mask (Engine/Shaders/Outline.hlsl).
    class SelectionOutline
    {
    public:
        SelectionOutline();
        ~SelectionOutline();

        SelectionOutline(const SelectionOutline&) = delete;
        SelectionOutline& operator=(const SelectionOutline&) = delete;

        /// Returns false and logs the reason on failure.
        [[nodiscard]] bool init(Renderer& renderer);
        void shutdown();

        /// Main thread: resizes the mask to the scene targets, after the scene target has its size for
        /// the frame. Waits for the render thread first when it does.
        void prepare(Renderer& renderer);
        /// Main thread: collects the meshes of `selected` (null for none) and everything below it,
        /// for the frame being built, holding their models until it has been drawn.
        void extract(const GameObject* selected);
        /// Main thread, while no frame is being recorded: hands what extract() collected to render().
        void sync();
        /// Records both passes, or nothing when no mesh was collected. Must be called outside a render
        /// pass, after the scene has been drawn.
        void render(Renderer& renderer);

        /// Written to the display target as is, after tone mapping: display-ready (sRGB-encoded).
        Vec4 color{1.0f, 0.55f, 0.1f, 1.0f};
        /// Pixels, 1 to 4. Multiplied by the display scale, so it looks the same on high-DPI screens.
        int thickness = 2;

    private:
        struct MaskItem;
        struct Frame
        {
            std::vector<MaskItem> items;
            Vec4 color;
            int radius = 1;   // Pixels.
        };

        std::unique_ptr<rhi::Pipeline> m_maskPipeline;
        std::unique_ptr<rhi::Pipeline> m_outlinePipeline;
        std::unique_ptr<rhi::Texture> m_mask;
        std::unique_ptr<Frame> m_next;
        std::unique_ptr<Frame> m_frame;
    };
}

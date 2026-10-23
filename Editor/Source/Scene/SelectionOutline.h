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

        /// Resizes the mask to the scene target. Must be called outside a frame, after the scene
        /// target has its size for the frame.
        void prepare(Renderer& renderer);

        /// Records both passes, or nothing when `selected` is null or has no visible mesh. Must be
        /// called outside a render pass, after the scene has been drawn.
        void render(Renderer& renderer, const GameObject* selected);

        /// Written to the scene target as is, which holds display-ready (sRGB-encoded) values.
        Vec4 color{1.0f, 0.55f, 0.1f, 1.0f};
        /// Pixels, 1 to 4. Multiplied by the display scale, so it looks the same on high-DPI screens.
        int thickness = 2;

    private:
        struct MaskItem;

        std::unique_ptr<rhi::Pipeline> m_maskPipeline;
        std::unique_ptr<rhi::Pipeline> m_outlinePipeline;
        std::unique_ptr<rhi::Texture> m_mask;
        std::vector<MaskItem> m_items;
    };
}

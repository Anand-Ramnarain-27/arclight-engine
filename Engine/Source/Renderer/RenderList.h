#pragma once

#include "Math/Math.h"

#include <cstdint>
#include <span>
#include <string_view>
#include <vector>

namespace arc
{
    class Model;

    /// A world transform shared by every primitive of one mesh in the list.
    struct RenderTransform
    {
        Mat4 world;   // Object space to world space.
        /// The transform mirrors (negative determinant), which turns counter-clockwise triangles
        /// clockwise on screen.
        bool mirrored = false;
    };

    /// One primitive to draw: one draw call.
    struct RenderItem
    {
        const Model* model = nullptr;
        uint32_t primitive = 0;   // Into model->primitives().
        uint32_t transform = 0;   // Into RenderList::transforms().
        /// Opaque items are drawn in ascending key order; see RenderList::sort().
        uint64_t sortKey = 0;
        /// World units along the camera's forward axis to the primitive's bounds centre.
        float viewDepth = 0.0f;
    };

    /// The frame's draws as a flat list, built from the scene and sorted for drawing.
    ///
    /// Primitives with blended materials go in a queue of their own, drawn after the opaque and
    /// masked ones. Within the opaque queue, sorting groups draws that share state, so each state
    /// change happens once per group: the pipeline first (the most expensive change), then the
    /// geometry block and model (buffer binds), then the material (textures and constants), and last
    /// distance, front to back, so nearer surfaces fill the depth buffer first and hide what is
    /// behind before it is shaded. Blended items are sorted back to front instead, which blending
    /// needs to look right, whatever that costs in state changes.
    class RenderList
    {
    public:
        void clear();

        /// Adds every primitive of the model's mesh.
        void addMesh(const Model& model, uint32_t mesh, const Mat4& world);

        /// Computes view depths and sort keys, then sorts both queues. With `sortOpaque` false the
        /// opaque queue keeps the order items were added in, to compare the state changes.
        void sort(const Vec3& cameraPosition, const Vec3& cameraForward, bool sortOpaque = true);

        [[nodiscard]] std::span<const RenderItem> opaque() const { return m_opaque; }
        [[nodiscard]] std::span<const RenderItem> blended() const { return m_blended; }
        [[nodiscard]] std::span<const RenderTransform> transforms() const { return m_transforms; }
        [[nodiscard]] bool empty() const { return m_opaque.empty() && m_blended.empty(); }

        /// Key layout, high bits first: pipeline state (2 bits: double-sided, mirrored) | geometry
        /// block (6) | model id (20) | material (12) | view depth (24).
        [[nodiscard]] static uint64_t opaqueSortKey(const RenderItem& item, const RenderTransform& transform);

    private:
        std::vector<RenderItem> m_opaque;
        std::vector<RenderItem> m_blended;
        std::vector<RenderTransform> m_transforms;
    };
}

#include "Renderer/RenderList.h"

#include "Renderer/Model.h"

#include <algorithm>
#include <bit>

namespace arc
{
    namespace
    {
        // A non-negative float's bit pattern increases with its value, so its top bits order
        // depths without any range to normalise against. Negative depths (behind the camera) are 0.
        uint64_t depthBits(float depth, int bits)
        {
            const float clamped = std::max(depth, 0.0f);
            return uint64_t{std::bit_cast<uint32_t>(clamped)} >> (32 - bits);
        }

        uint64_t field(uint64_t value, int bits)
        {
            return value & ((uint64_t{1} << bits) - 1);
        }
    }

    void RenderList::clear()
    {
        m_opaque.clear();
        m_blended.clear();
        m_transforms.clear();
    }

    void RenderList::addMesh(const Model& model, uint32_t mesh, const Mat4& world)
    {
        if (mesh >= model.meshes().size())
            return;
        const auto transform = static_cast<uint32_t>(m_transforms.size());
        m_transforms.push_back({world, determinant(world) < 0.0f});

        const MeshData& data = model.meshes()[mesh];
        for (uint32_t i = 0; i < data.primitiveCount; ++i)
        {
            const uint32_t primitive = data.firstPrimitive + i;
            const int32_t material = model.primitives()[primitive].material;
            const bool blended = material >= 0 && model.materials()[static_cast<size_t>(material)].alphaMode == AlphaMode::Blend;
            (blended ? m_blended : m_opaque).push_back({.model = &model, .primitive = primitive, .transform = transform});
        }
    }

    uint64_t RenderList::opaqueSortKey(const RenderItem& item, const RenderTransform& transform)
    {
        const Primitive& primitive = item.model->primitives()[item.primitive];
        const bool doubleSided =
            primitive.material >= 0 && item.model->materials()[static_cast<size_t>(primitive.material)].doubleSided;
        const uint64_t pipeline = uint64_t{doubleSided} | uint64_t{transform.mirrored} << 1;
        // -1, the default material, becomes 0; the others shift up by one.
        const auto material = static_cast<uint64_t>(primitive.material + 1);
        return field(pipeline, 2) << 62 | field(item.model->geometryBlock(), 6) << 56 | field(item.model->id(), 20) << 36 |
               field(material, 12) << 24 | depthBits(item.viewDepth, 24);
    }

    void RenderList::sort(const Vec3& cameraPosition, const Vec3& cameraForward, bool sortOpaque)
    {
        const auto computeDepth = [&](RenderItem& item) {
            const Primitive& primitive = item.model->primitives()[item.primitive];
            const Vec3 centre = m_transforms[item.transform].world.transformPoint(primitive.bounds.center());
            item.viewDepth = dot(centre - cameraPosition, cameraForward);
        };

        for (RenderItem& item : m_opaque)
        {
            computeDepth(item);
            item.sortKey = opaqueSortKey(item, m_transforms[item.transform]);
        }
        if (sortOpaque)
            std::sort(m_opaque.begin(), m_opaque.end(), [](const RenderItem& a, const RenderItem& b) { return a.sortKey < b.sortKey; });

        for (RenderItem& item : m_blended)
            computeDepth(item);
        // Stable, so surfaces at equal depth keep the order they were added in, frame after frame,
        // instead of flickering between orders.
        std::stable_sort(m_blended.begin(), m_blended.end(),
                         [](const RenderItem& a, const RenderItem& b) { return a.viewDepth > b.viewDepth; });
    }
}

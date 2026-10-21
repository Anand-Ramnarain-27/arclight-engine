#pragma once

#include "Renderer/GeometryPool.h"
#include "Renderer/ModelData.h"

#include <memory>
#include <string>
#include <vector>

namespace arc
{
    namespace rhi
    {
        class Buffer;
        class Device;
        class Sampler;
        class Texture;
    }

    /// Where one primitive's indices are in its model's GeometryPool block, and how to draw them.
    struct PrimitiveDraw
    {
        rhi::IndexFormat indexFormat = rhi::IndexFormat::UInt32;
        uint32_t firstIndex = 0;   // Into the block's index buffer of that format.
        uint32_t indexCount = 0;
        int32_t baseVertex = 0;    // Into the block's vertex buffer; added to every index.
    };

    /// A model on the GPU: its vertices and indices in a GeometryPool block, the textures its
    /// materials draw with, and the CPU-side description of what to draw. Drawn by MeshRenderer.
    ///
    /// A primitive whose indices all fit in 16 bits has them stored as 16-bit, which halves its
    /// index memory and bandwidth; the others keep 32-bit indices.
    class Model
    {
    public:
        /// Uploads the model into `geometry`, which must outlive it. Textures whose image is missing
        /// or could not be decoded are left null, and MeshRenderer draws them with its "missing"
        /// texture. Returns nullptr and logs if the geometry cannot be uploaded.
        [[nodiscard]] static std::unique_ptr<Model> create(rhi::Device& device, GeometryPool& geometry, const ModelData& data);

        ~Model();

        Model(const Model&) = delete;
        Model& operator=(const Model&) = delete;

        [[nodiscard]] const std::string& name() const { return m_name; }
        [[nodiscard]] GeometryPool& geometry() const { return *m_geometry; }
        /// The pool block holding every primitive's vertices and indices.
        [[nodiscard]] uint32_t geometryBlock() const { return m_allocation.block; }
        /// Indexed like primitives().
        [[nodiscard]] const std::vector<PrimitiveDraw>& primitiveDraws() const { return m_draws; }
        /// Unique among the models created in this run; used to group draws by model in sort keys.
        [[nodiscard]] uint32_t id() const { return m_id; }

        [[nodiscard]] const std::vector<Primitive>& primitives() const { return m_primitives; }
        [[nodiscard]] const std::vector<MeshData>& meshes() const { return m_meshes; }
        [[nodiscard]] const std::vector<MeshInstance>& instances() const { return m_instances; }
        [[nodiscard]] const std::vector<NodeData>& nodes() const { return m_nodes; }

        /// A CPU copy of every vertex position and index, as in the GPU buffers, for picking and other
        /// queries that cannot read GPU memory. Primitive::baseVertex applies to these indices too.
        [[nodiscard]] const std::vector<Vec3>& positions() const { return m_positions; }
        [[nodiscard]] const std::vector<uint32_t>& indices() const { return m_indices; }
        [[nodiscard]] const std::vector<MaterialData>& materials() const { return m_materials; }
        [[nodiscard]] const Aabb& bounds() const { return m_bounds; }

        /// Indexed like ModelData::textures. Null for a texture that no drawn material slot uses, or
        /// whose image is unavailable; then sampler() is null too.
        [[nodiscard]] rhi::Texture* texture(int32_t index) const;
        [[nodiscard]] rhi::Sampler* sampler(int32_t index) const;

        /// Object space: the union of the mesh's primitive bounds. Empty for an index out of range.
        [[nodiscard]] Aabb meshBounds(uint32_t mesh) const;
        /// One draw of the mesh. Zero for an index out of range.
        [[nodiscard]] uint32_t meshTriangleCount(uint32_t mesh) const;

        [[nodiscard]] uint32_t vertexCount() const { return m_vertexCount; }
        [[nodiscard]] uint32_t triangleCount() const { return m_triangleCount; }
        [[nodiscard]] uint32_t textureCount() const;

    private:
        Model() = default;

        std::string m_name;
        uint32_t m_id = 0;
        GeometryPool* m_geometry = nullptr;
        GeometryAllocation m_allocation;
        std::vector<PrimitiveDraw> m_draws;
        std::vector<Primitive> m_primitives;
        std::vector<MeshData> m_meshes;
        std::vector<MeshInstance> m_instances;
        std::vector<NodeData> m_nodes;
        std::vector<Vec3> m_positions;
        std::vector<uint32_t> m_indices;
        std::vector<MaterialData> m_materials;
        std::vector<std::unique_ptr<rhi::Texture>> m_textures;
        /// Textures with the same sampling settings share a sampler: D3D12's sampler descriptors are
        /// a small fixed pool. m_textureSamplers points into m_samplers, per texture.
        std::vector<std::unique_ptr<rhi::Sampler>> m_samplers;
        std::vector<rhi::Sampler*> m_textureSamplers;
        Aabb m_bounds;
        uint32_t m_vertexCount = 0;
        uint32_t m_triangleCount = 0;
    };
}

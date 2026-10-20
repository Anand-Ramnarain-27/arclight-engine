#pragma once

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

    /// A model on the GPU: one vertex buffer and one index buffer shared by every primitive, the
    /// textures its materials draw with, and the CPU-side description of what to draw.
    /// Drawn by MeshRenderer.
    class Model
    {
    public:
        /// Uploads the model. Textures whose image is missing or could not be decoded are left null,
        /// and MeshRenderer draws them with its "missing" texture. Returns nullptr and logs if the
        /// buffers cannot be created.
        [[nodiscard]] static std::unique_ptr<Model> create(rhi::Device& device, const ModelData& data);

        ~Model();

        Model(const Model&) = delete;
        Model& operator=(const Model&) = delete;

        [[nodiscard]] const std::string& name() const { return m_name; }
        [[nodiscard]] rhi::Buffer& vertexBuffer() const { return *m_vertexBuffer; }
        [[nodiscard]] rhi::Buffer& indexBuffer() const { return *m_indexBuffer; }

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
        std::unique_ptr<rhi::Buffer> m_vertexBuffer;
        std::unique_ptr<rhi::Buffer> m_indexBuffer;
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

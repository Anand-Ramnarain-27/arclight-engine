#include "Renderer/Model.h"

#include "Core/Log.h"
#include "RHI/Buffer.h"
#include "RHI/BufferUtils.h"
#include "RHI/Device.h"
#include "RHI/Sampler.h"
#include "RHI/Texture.h"
#include "Renderer/TextureLoader.h"

#include <algorithm>
#include <span>
#include <string>

namespace arc
{
    namespace
    {
        // Everything but the debug name: textures with equal settings share one sampler, since the
        // device's sampler descriptors are a small, fixed pool on D3D12.
        bool sameSampling(const rhi::SamplerDesc& a, const rhi::SamplerDesc& b)
        {
            return a.minFilter == b.minFilter && a.magFilter == b.magFilter && a.mipFilter == b.mipFilter &&
                   a.addressU == b.addressU && a.addressV == b.addressV && a.addressW == b.addressW &&
                   a.borderColor == b.borderColor && a.maxAnisotropy == b.maxAnisotropy && a.minLod == b.minLod &&
                   a.maxLod == b.maxLod;
        }
    }

    Model::~Model() = default;

    std::unique_ptr<Model> Model::create(rhi::Device& device, const ModelData& data)
    {
        if (data.vertices.empty() || data.indices.empty())
        {
            ARC_LOG_ERROR("{}: there is nothing to draw", data.name);
            return nullptr;
        }

        std::unique_ptr<Model> model(new Model());
        model->m_name = data.name;
        model->m_vertexBuffer = device.createBuffer({
            .size = data.vertices.size() * sizeof(MeshVertex),
            .usage = rhi::BufferUsage::Vertex,
            .memory = rhi::MemoryType::GpuOnly,
            .debugName = data.name + " Vertices",
        });
        model->m_indexBuffer = device.createBuffer({
            .size = data.indices.size() * sizeof(uint32_t),
            .usage = rhi::BufferUsage::Index,
            .memory = rhi::MemoryType::GpuOnly,
            .debugName = data.name + " Indices",
        });
        if (!model->m_vertexBuffer || !model->m_indexBuffer ||
            !rhi::uploadBuffer(device, *model->m_vertexBuffer, std::as_bytes(std::span(data.vertices))) ||
            !rhi::uploadBuffer(device, *model->m_indexBuffer, std::as_bytes(std::span(data.indices))))
            return nullptr;

        model->m_primitives = data.primitives;
        model->m_meshes = data.meshes;
        model->m_instances = data.instances;
        model->m_nodes = data.nodes;
        model->m_indices = data.indices;
        model->m_positions.reserve(data.vertices.size());
        for (const MeshVertex& vertex : data.vertices)
            model->m_positions.push_back(vertex.position);
        model->m_materials = data.materials;
        model->m_bounds = data.bounds;
        model->m_vertexCount = static_cast<uint32_t>(data.vertices.size());
        for (const MeshInstance& instance : data.instances)
        {
            const MeshData& mesh = data.meshes[instance.mesh];
            for (uint32_t i = 0; i < mesh.primitiveCount; ++i)
                model->m_triangleCount += data.primitives[mesh.firstPrimitive + i].indexCount / 3;
        }

        // Only base colour textures are drawn so far, and they hold colour, so they are created as
        // sRGB: the GPU decodes them to linear before filtering.
        std::vector<bool> isBaseColor(data.textures.size(), false);
        for (const MaterialData& material : data.materials)
            if (material.baseColorTexture >= 0)
                isBaseColor[static_cast<size_t>(material.baseColorTexture)] = true;

        model->m_textures.resize(data.textures.size());
        model->m_textureSamplers.resize(data.textures.size(), nullptr);
        for (size_t i = 0; i < data.textures.size(); ++i)
        {
            const TextureData& texture = data.textures[i];
            if (!isBaseColor[i] || texture.image < 0 || data.images[static_cast<size_t>(texture.image)].empty())
                continue;

            const std::vector<Image> levels = generateMipChain(data.images[static_cast<size_t>(texture.image)], ColorSpace::Srgb);
            model->m_textures[i] = createTexture(device, levels, ColorSpace::Srgb, data.name + " Texture " + std::to_string(i));

            const auto shared = std::find_if(model->m_samplers.begin(), model->m_samplers.end(),
                                             [&](const auto& sampler) { return sameSampling(sampler->desc(), texture.sampler); });
            if (shared != model->m_samplers.end())
            {
                model->m_textureSamplers[i] = shared->get();
            }
            else if (auto sampler = device.createSampler(texture.sampler))
            {
                model->m_textureSamplers[i] = sampler.get();
                model->m_samplers.push_back(std::move(sampler));
            }
        }
        return model;
    }

    rhi::Texture* Model::texture(int32_t index) const
    {
        return index >= 0 && static_cast<size_t>(index) < m_textures.size() ? m_textures[static_cast<size_t>(index)].get()
                                                                              : nullptr;
    }

    rhi::Sampler* Model::sampler(int32_t index) const
    {
        return index >= 0 && static_cast<size_t>(index) < m_textureSamplers.size()
                   ? m_textureSamplers[static_cast<size_t>(index)]
                   : nullptr;
    }

    Aabb Model::meshBounds(uint32_t mesh) const
    {
        Aabb bounds;
        if (mesh < m_meshes.size())
            for (uint32_t i = 0; i < m_meshes[mesh].primitiveCount; ++i)
                bounds.expand(m_primitives[m_meshes[mesh].firstPrimitive + i].bounds);
        return bounds;
    }

    uint32_t Model::meshTriangleCount(uint32_t mesh) const
    {
        uint32_t triangles = 0;
        if (mesh < m_meshes.size())
            for (uint32_t i = 0; i < m_meshes[mesh].primitiveCount; ++i)
                triangles += m_primitives[m_meshes[mesh].firstPrimitive + i].indexCount / 3;
        return triangles;
    }

    uint32_t Model::textureCount() const
    {
        return static_cast<uint32_t>(std::count_if(m_textures.begin(), m_textures.end(), [](const auto& t) { return t != nullptr; }));
    }
}

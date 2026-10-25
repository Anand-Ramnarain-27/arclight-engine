#include "Renderer/Model.h"

#include "Core/Log.h"
#include "RHI/Device.h"
#include "RHI/Sampler.h"
#include "RHI/Texture.h"
#include "Renderer/TextureLoader.h"

#include <algorithm>
#include <limits>
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

    // Frames in flight may still draw the model; the pool keeps its ranges until they finish.
    Model::~Model()
    {
        if (m_geometry)
            m_geometry->free(m_allocation);
    }

    std::unique_ptr<Model> Model::create(rhi::Device& device, GeometryPool& geometry, const ModelData& data, Uuid asset)
    {
        if (data.vertices.empty() || data.indices.empty())
        {
            ARC_LOG_ERROR("{}: there is nothing to draw", data.name);
            return nullptr;
        }

        // Indices are relative to the primitive's first vertex, so a primitive with at most 65536
        // vertices can always use 16-bit indices. Checking the largest index catches the rest too.
        // Simplified levels index the same vertices, so the full-detail level decides for all.
        std::vector<uint16_t> indices16;
        std::vector<uint32_t> indices32;
        std::vector<PrimitiveLods> lods;
        lods.reserve(data.primitives.size());
        for (const Primitive& primitive : data.primitives)
        {
            const auto first = data.indices.begin() + primitive.firstIndex;
            const uint32_t largest = primitive.indexCount > 0 ? *std::max_element(first, first + primitive.indexCount) : 0;
            const bool use16 = largest <= std::numeric_limits<uint16_t>::max();

            PrimitiveLods levels;
            levels.count = 1 + primitive.lodCount;
            for (uint32_t level = 0; level < levels.count; ++level)
            {
                const uint32_t firstIndex = level == 0 ? primitive.firstIndex : primitive.lods[level - 1].firstIndex;
                const uint32_t indexCount = level == 0 ? primitive.indexCount : primitive.lods[level - 1].indexCount;
                levels.errors[level] = level == 0 ? 0.0f : primitive.lods[level - 1].error;

                const auto begin = data.indices.begin() + firstIndex;
                const auto end = begin + indexCount;
                PrimitiveDraw& draw = levels.levels[level];
                draw = {.indexCount = indexCount, .baseVertex = static_cast<int32_t>(primitive.baseVertex)};
                if (use16)
                {
                    draw.indexFormat = rhi::IndexFormat::UInt16;
                    draw.firstIndex = static_cast<uint32_t>(indices16.size());
                    for (auto it = begin; it != end; ++it)
                        indices16.push_back(static_cast<uint16_t>(*it));
                }
                else
                {
                    draw.firstIndex = static_cast<uint32_t>(indices32.size());
                    indices32.insert(indices32.end(), begin, end);
                }
            }
            lods.push_back(levels);
        }

        const std::optional<GeometryAllocation> allocation =
            geometry.allocate(static_cast<uint32_t>(data.vertices.size()), static_cast<uint32_t>(indices16.size()),
                              static_cast<uint32_t>(indices32.size()), data.name);
        if (!allocation)
            return nullptr;
        if (!geometry.upload(*allocation, data.vertices, indices16, indices32))
        {
            geometry.free(*allocation);
            return nullptr;
        }
        // Offsets so far were within the model's own ranges; the pool's ranges start further in.
        for (PrimitiveLods& levels : lods)
            for (uint32_t level = 0; level < levels.count; ++level)
            {
                PrimitiveDraw& draw = levels.levels[level];
                draw.firstIndex += draw.indexFormat == rhi::IndexFormat::UInt16 ? allocation->indices16.offset
                                                                                : allocation->indices32.offset;
                draw.baseVertex += static_cast<int32_t>(allocation->vertices.offset);
            }

        static uint32_t nextId = 1;
        std::unique_ptr<Model> model(new Model());
        model->m_name = data.name;
        model->m_id = nextId++;
        model->m_asset = asset;
        model->m_geometry = &geometry;
        model->m_allocation = *allocation;
        model->m_lods = std::move(lods);

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

        // Only textures a material slot draws are created. sRGB images get an sRGB format, so the GPU
        // decodes them to linear before filtering; data stays as stored.
        std::vector<bool> isUsed(data.textures.size(), false);
        for (const MaterialData& material : data.materials)
            for (const int32_t texture : {material.baseColorTexture, material.metallicRoughnessTexture})
                if (texture >= 0)
                    isUsed[static_cast<size_t>(texture)] = true;

        model->m_textures.resize(data.textures.size());
        model->m_textureSamplers.resize(data.textures.size(), nullptr);
        for (size_t i = 0; i < data.textures.size(); ++i)
        {
            const TextureData& texture = data.textures[i];
            if (!isUsed[i] || texture.image < 0)
                continue;
            const auto image = static_cast<size_t>(texture.image);
            const ColorSpace colorSpace = image < data.imageColorSpaces.size() ? data.imageColorSpaces[image] : ColorSpace::Srgb;
            const bool hasMips = image < data.imageMips.size() && !data.imageMips[image].empty();
            const bool hasImage = image < data.images.size() && !data.images[image].empty();
            if (!hasMips && !hasImage)
                continue;

            const std::vector<Image> generated = hasMips ? std::vector<Image>() : generateMipChain(data.images[image], colorSpace);
            const std::vector<Image>& levels = hasMips ? data.imageMips[image] : generated;
            model->m_textures[i] = createTexture(device, levels, colorSpace, data.name + " Texture " + std::to_string(i));

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

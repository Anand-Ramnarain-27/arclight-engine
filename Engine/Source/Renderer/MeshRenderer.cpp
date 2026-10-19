#include "Renderer/MeshRenderer.h"

#include "Core/Log.h"
#include "RHI/CommandList.h"
#include "RHI/Device.h"
#include "RHI/Pipeline.h"
#include "RHI/Sampler.h"
#include "RHI/Shader.h"
#include "RHI/Texture.h"
#include "Renderer/Model.h"
#include "Renderer/Renderer.h"
#include "Renderer/ShaderLibrary.h"
#include "Renderer/TextureLoader.h"

#include <cmath>
#include <cstddef>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace arc
{
    namespace
    {
        // Mirrors DrawConstants in Mesh.hlsl. The uint after the two matrices starts a new 16-byte
        // row; HLSL pads the cbuffer to a whole row, so the C++ struct spells out the same padding.
        struct MeshDrawConstants
        {
            Mat4 world;
            Mat4 normalMatrix;
            uint32_t debugView = 0;
            uint32_t padding[3]{};
        };
        static_assert(offsetof(MeshDrawConstants, normalMatrix) == 64);
        static_assert(offsetof(MeshDrawConstants, debugView) == 128);
        static_assert(sizeof(MeshDrawConstants) == 144);

        // Mirrors MaterialConstants in Mesh.hlsl: a float4 row, then two scalars packed into the
        // next row.
        struct MaterialConstants
        {
            Vec4 baseColorFactor;
            float alphaCutoff = 0.5f;
            uint32_t alphaMode = 0;
            float padding[2]{};
        };
        static_assert(offsetof(MaterialConstants, alphaCutoff) == 16);
        static_assert(offsetof(MaterialConstants, alphaMode) == 20);
        static_assert(sizeof(MaterialConstants) == 32);

        Image solidImage(Extent2D size, uint32_t cellSize, std::array<uint8_t, 4> a, std::array<uint8_t, 4> b)
        {
            Image image;
            image.size = size;
            image.pixels.resize(size_t{size.width} * size.height * 4);
            for (uint32_t y = 0; y < size.height; ++y)
                for (uint32_t x = 0; x < size.width; ++x)
                {
                    const std::array<uint8_t, 4>& color = ((x / cellSize + y / cellSize) % 2 == 0) ? a : b;
                    for (size_t c = 0; c < 4; ++c)
                        image.pixels[(size_t{y} * size.width + x) * 4 + c] = std::byte{color[c]};
                }
            return image;
        }

        // The inverse transpose undoes non-uniform scale for normals, but a zero scale (a common way
        // to hide a node) has no inverse; the world matrix is the usable fallback there.
        Mat4 normalMatrixOf(const Mat4& world)
        {
            return std::abs(determinant(world)) > 1e-12f ? normalMatrix(world) : world;
        }
    }

    MeshRenderer::MeshRenderer() = default;

    // Defined here, where the RHI types are complete, for the unique_ptr destructors.
    MeshRenderer::~MeshRenderer() = default;

    bool MeshRenderer::init(Renderer& renderer)
    {
        rhi::Device& device = renderer.device();
        const auto vertexShader = renderer.shaders().load("Mesh", rhi::ShaderStage::Vertex, "VSMain");
        const auto pixelShader = renderer.shaders().load("Mesh", rhi::ShaderStage::Pixel, "PSMain");
        if (!vertexShader || !pixelShader)
            return false;

        rhi::PipelineDesc desc;
        desc.vertexShader = vertexShader.get();
        desc.pixelShader = pixelShader.get();
        desc.vertexLayout = {
            .stride = sizeof(MeshVertex),
            .attributes = {
                {.semantic = "POSITION", .format = rhi::VertexFormat::Float3, .offset = offsetof(MeshVertex, position)},
                {.semantic = "NORMAL", .format = rhi::VertexFormat::Float3, .offset = offsetof(MeshVertex, normal)},
                {.semantic = "TEXCOORD", .format = rhi::VertexFormat::Float2, .offset = offsetof(MeshVertex, uv)},
            },
        };
        desc.colorFormat = Renderer::sceneFormat();
        desc.depthFormat = Renderer::sceneDepthFormat();

        for (size_t index = 0; index < m_pipelines.size(); ++index)
        {
            const PipelineKey key{.reversedZ = (index & 1) != 0,
                                  .doubleSided = (index & 2) != 0,
                                  .blend = (index & 4) != 0,
                                  .mirrored = (index & 8) != 0};
            Camera camera;
            camera.reversedZ = key.reversedZ;
            // Blended surfaces are tested against depth but do not write it, so whatever is drawn
            // after them still shows through.
            desc.depth = {.test = true, .write = !key.blend, .compare = camera.depthCompare()};
            desc.cullMode = key.doubleSided ? rhi::CullMode::None : rhi::CullMode::Back;
            // glTF: when a node's transform has a negative determinant, its front faces are the
            // clockwise ones. Flipping the winding rule, rather than the culled side, also keeps
            // SV_IsFrontFace right for double-sided materials.
            desc.frontCounterClockwise = !key.mirrored;
            desc.blend = key.blend ? rhi::BlendMode::AlphaBlend : rhi::BlendMode::Opaque;
            desc.debugName = std::string("Mesh") + (key.doubleSided ? " Double-Sided" : "") + (key.blend ? " Blended" : "") +
                             (key.mirrored ? " Mirrored" : "") + (key.reversedZ ? " (Reversed-Z)" : "");
            m_pipelines[index] = device.createPipeline(desc);
            if (!m_pipelines[index])
                return false;
        }

        const Image white = solidImage({1, 1}, 1, {255, 255, 255, 255}, {255, 255, 255, 255});
        const Image missing = solidImage({16, 16}, 4, {255, 0, 255, 255}, {20, 20, 20, 255});
        m_whiteTexture = createTexture(device, std::span(&white, 1), ColorSpace::Srgb, "White");
        m_missingTexture = createTexture(device, std::span(&missing, 1), ColorSpace::Srgb, "Missing Texture");

        rhi::SamplerDesc samplerDesc;
        samplerDesc.maxAnisotropy = 8;
        samplerDesc.debugName = "Mesh Default Sampler";
        m_defaultSampler = device.createSampler(samplerDesc);
        return m_whiteTexture && m_missingTexture && m_defaultSampler;
    }

    void MeshRenderer::shutdown()
    {
        for (auto& pipeline : m_pipelines)
            pipeline.reset();
        m_whiteTexture.reset();
        m_missingTexture.reset();
        m_defaultSampler.reset();
    }

    void MeshRenderer::draw(Renderer& renderer, const Model& model, const Mat4& transform, DebugView debugView)
    {
        std::vector<MeshDrawItem> items;
        items.reserve(model.instances().size());
        for (const MeshInstance& instance : model.instances())
            items.push_back({.model = &model, .mesh = instance.mesh, .world = transform * instance.world, .debugName = model.name()});
        draw(renderer, items, debugView);
    }

    void MeshRenderer::draw(Renderer& renderer, std::span<const MeshDrawItem> items, DebugView debugView)
    {
        rhi::CommandList& commandList = renderer.commandList();
        const bool reversedZ = renderer.sceneCamera().reversedZ;
        const MaterialData defaultMaterial;
        commandList.setConstants(0, renderer.sceneViewConstants());

        rhi::Pipeline* currentPipeline = nullptr;
        const Model* boundModel = nullptr;

        // Two passes over the items: opaque and masked first, then blended, so blended surfaces
        // land on top of everything solid behind them.
        for (const bool blendPass : {false, true})
        {
            for (const MeshDrawItem& item : items)
            {
                const Model& model = *item.model;
                if (item.mesh >= model.meshes().size())
                    continue;
                const MeshData& mesh = model.meshes()[item.mesh];
                const bool mirrored = determinant(item.world) < 0.0f;
                bool itemConstantsSet = false;
                std::optional<rhi::ScopedDebugGroup> group;

                for (uint32_t i = 0; i < mesh.primitiveCount; ++i)
                {
                    const Primitive& primitive = model.primitives()[mesh.firstPrimitive + i];
                    const MaterialData& material =
                        primitive.material >= 0 ? model.materials()[static_cast<size_t>(primitive.material)] : defaultMaterial;
                    if ((material.alphaMode == AlphaMode::Blend) != blendPass)
                        continue;

                    if (!group)
                        group.emplace(commandList, item.debugName);

                    rhi::Pipeline* wanted = m_pipelines[PipelineKey{.reversedZ = reversedZ,
                                                                    .doubleSided = material.doubleSided,
                                                                    .blend = blendPass,
                                                                    .mirrored = mirrored}
                                                            .index()]
                                                .get();
                    if (wanted != currentPipeline)
                    {
                        commandList.setPipeline(*wanted);
                        currentPipeline = wanted;
                    }
                    // Buffer bindings survive pipeline changes; they need a pipeline set first only
                    // because the vertex stride comes from its layout.
                    if (boundModel != &model)
                    {
                        commandList.setVertexBuffer(model.vertexBuffer());
                        commandList.setIndexBuffer(model.indexBuffer(), rhi::IndexFormat::UInt32);
                        boundModel = &model;
                    }

                    if (!itemConstantsSet)
                    {
                        commandList.setConstants(1, MeshDrawConstants{
                                                        .world = item.world,
                                                        .normalMatrix = normalMatrixOf(item.world),
                                                        .debugView = static_cast<uint32_t>(debugView),
                                                    });
                        itemConstantsSet = true;
                    }

                    rhi::Texture* texture = m_whiteTexture.get();
                    rhi::Sampler* sampler = m_defaultSampler.get();
                    if (material.baseColorTexture >= 0)
                    {
                        texture = model.texture(material.baseColorTexture);
                        sampler = model.sampler(material.baseColorTexture);
                        if (!texture || !sampler)
                        {
                            texture = m_missingTexture.get();
                            sampler = m_defaultSampler.get();
                        }
                    }
                    commandList.setTexture(0, *texture);
                    commandList.setSampler(0, *sampler);
                    commandList.setConstants(2, MaterialConstants{
                                                    .baseColorFactor = material.baseColorFactor,
                                                    .alphaCutoff = material.alphaCutoff,
                                                    .alphaMode = static_cast<uint32_t>(material.alphaMode),
                                                });
                    commandList.drawIndexed(primitive.indexCount, 1, primitive.firstIndex,
                                            static_cast<int32_t>(primitive.baseVertex));
                }
            }
        }
    }
}

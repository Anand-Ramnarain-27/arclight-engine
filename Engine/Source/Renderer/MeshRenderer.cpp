#include "Renderer/MeshRenderer.h"

#include "Core/Log.h"
#include "RHI/Buffer.h"
#include "RHI/CommandList.h"
#include "RHI/Device.h"
#include "RHI/Pipeline.h"
#include "RHI/Sampler.h"
#include "RHI/Shader.h"
#include "RHI/Texture.h"
#include "RHI/TextureUtils.h"
#include "Renderer/GeometryPool.h"
#include "Renderer/Model.h"
#include "Renderer/RenderList.h"
#include "Renderer/Renderer.h"
#include "Renderer/ShaderLibrary.h"
#include "Renderer/TextureLoader.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>
#include <string>
#include <utility>

namespace arc
{
    namespace
    {
        // Mirrors DrawConstants in Mesh.hlsl. The scalars after the two matrices start a new 16-byte
        // row; HLSL pads the cbuffer to a whole row, so the C++ struct spells out the same padding.
        struct MeshDrawConstants
        {
            Mat4 world;
            Mat4 normalMatrix;
            uint32_t debugView = 0;
            float handedness = 1.0f;
            uint32_t padding[2]{};
        };
        static_assert(offsetof(MeshDrawConstants, normalMatrix) == 64);
        static_assert(offsetof(MeshDrawConstants, debugView) == 128);
        static_assert(offsetof(MeshDrawConstants, handedness) == 132);
        static_assert(sizeof(MeshDrawConstants) == 144);

        // Mirrors MaterialConstants in Mesh.hlsl: a float4 row, then scalars and a float3, packed
        // four to a row; the float3 shares its row with the scalar after it.
        struct MaterialConstants
        {
            Vec4 baseColorFactor;
            float alphaCutoff = 0.5f;
            uint32_t alphaMode = 0;
            float roughnessFactor = 1.0f;
            float metallicFactor = 1.0f;
            Vec3 emissiveFactor;
            float normalScale = 1.0f;
            float occlusionStrength = 1.0f;
            uint32_t shadingModel = 0;
            uint32_t toonBands = 1;
            float toonSoftness = 0.0f;
            float toonSpecular = 0.0f;
            float toonRimSize = 0.0f;
            float toonRim = 0.0f;
            uint32_t toonFlags = 0;   // TOON_FLAG_ bits in Toon.hlsli.
        };
        static_assert(offsetof(MaterialConstants, alphaCutoff) == 16);
        static_assert(offsetof(MaterialConstants, alphaMode) == 20);
        static_assert(offsetof(MaterialConstants, roughnessFactor) == 24);
        static_assert(offsetof(MaterialConstants, metallicFactor) == 28);
        static_assert(offsetof(MaterialConstants, emissiveFactor) == 32);
        static_assert(offsetof(MaterialConstants, normalScale) == 44);
        static_assert(offsetof(MaterialConstants, occlusionStrength) == 48);
        static_assert(offsetof(MaterialConstants, shadingModel) == 52);
        static_assert(offsetof(MaterialConstants, toonSpecular) == 64);
        static_assert(offsetof(MaterialConstants, toonFlags) == 76);
        static_assert(sizeof(MaterialConstants) == 80);

        constexpr uint32_t kToonFlagRamp = 1;

        // Mirrors OutlineConstants in ToonOutline.hlsl.
        struct OutlineConstants
        {
            Vec3 color;
            float width = 0.0f;
            Vec2 viewportSize;
            float padding[2]{};
        };
        static_assert(offsetof(OutlineConstants, width) == 12);
        static_assert(offsetof(OutlineConstants, viewportSize) == 16);
        static_assert(sizeof(OutlineConstants) == 32);

        MaterialConstants materialConstants(const MaterialData& material, const MaterialStyle* style)
        {
            MaterialConstants constants{
                .baseColorFactor = material.baseColorFactor,
                .alphaCutoff = material.alphaCutoff,
                .alphaMode = static_cast<uint32_t>(material.alphaMode),
                .roughnessFactor = material.roughnessFactor,
                .metallicFactor = material.metallicFactor,
                .emissiveFactor = material.emissiveFactor,
                .normalScale = material.normalScale,
                .occlusionStrength = material.occlusionStrength,
            };
            if (style && style->shading == ShadingModel::Toon)
            {
                const ToonStyle& toon = style->toon;
                constants.shadingModel = static_cast<uint32_t>(ShadingModel::Toon);
                constants.toonBands = std::max(toon.bands, 1u);
                constants.toonSoftness = std::max(toon.softness, 0.0f);
                constants.toonSpecular = std::max(toon.specular, 0.0f);
                constants.toonRimSize = std::clamp(toon.rimSize, 0.0f, 1.0f);
                constants.toonRim = std::max(toon.rim, 0.0f);
                constants.toonFlags = toon.ramp ? kToonFlagRamp : 0u;
            }
            return constants;
        }

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
                {.semantic = "TANGENT", .format = rhi::VertexFormat::Float4, .offset = offsetof(MeshVertex, tangent)},
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

        const auto outlineVertexShader = renderer.shaders().load("ToonOutline", rhi::ShaderStage::Vertex, "VSMain");
        const auto outlinePixelShader = renderer.shaders().load("ToonOutline", rhi::ShaderStage::Pixel, "PSMain");
        if (!outlineVertexShader || !outlinePixelShader)
            return false;
        desc.vertexShader = outlineVertexShader.get();
        desc.pixelShader = outlinePixelShader.get();
        desc.blend = rhi::BlendMode::Opaque;
        for (size_t index = 0; index < m_outlinePipelines.size(); ++index)
        {
            const bool reversedZ = (index & 1) != 0;
            const bool mirrored = (index & 2) != 0;
            Camera camera;
            camera.reversedZ = reversedZ;
            desc.depth = {.test = true, .write = true, .compare = camera.depthCompare()};
            // The shell's front faces would cover the mesh; its back faces are the outline.
            desc.cullMode = rhi::CullMode::Front;
            desc.frontCounterClockwise = !mirrored;
            desc.debugName = std::string("Toon Outline") + (mirrored ? " Mirrored" : "") + (reversedZ ? " (Reversed-Z)" : "");
            m_outlinePipelines[index] = device.createPipeline(desc);
            if (!m_outlinePipelines[index])
                return false;
        }

        const Image white = solidImage({1, 1}, 1, {255, 255, 255, 255}, {255, 255, 255, 255});
        const Image missing = solidImage({16, 16}, 4, {255, 0, 255, 255}, {20, 20, 20, 255});
        m_whiteTexture = createTexture(device, std::span(&white, 1), ColorSpace::Srgb, "White");
        // (0, 0, 1) in tangent space, stored as * 0.5 + 0.5: the surface's own normal.
        const Image flatNormal = solidImage({1, 1}, 1, {128, 128, 255, 255}, {128, 128, 255, 255});
        m_flatNormalTexture = createTexture(device, std::span(&flatNormal, 1), ColorSpace::Linear, "Flat Normal");
        m_missingTexture = createTexture(device, std::span(&missing, 1), ColorSpace::Srgb, "Missing Texture");

        m_blackCube = device.createTexture({
            .size = {1, 1},
            .format = rhi::Format::RGBA8_UNORM,
            .type = rhi::TextureType::Cube,
            .debugName = "Black Cube",
        });
        if (m_blackCube)
        {
            const std::array<std::byte, 4> black{std::byte{0}, std::byte{0}, std::byte{0}, std::byte{255}};
            const std::array<std::span<const std::byte>, 6> faces{black, black, black, black, black, black};
            if (!rhi::uploadTexture(device, *m_blackCube, faces))
                m_blackCube.reset();
        }

        rhi::SamplerDesc samplerDesc;
        samplerDesc.maxAnisotropy = 8;
        samplerDesc.debugName = "Mesh Default Sampler";
        m_defaultSampler = device.createSampler(samplerDesc);
        m_clampSampler = device.createSampler({.addressU = rhi::AddressMode::ClampToEdge,
                                               .addressV = rhi::AddressMode::ClampToEdge,
                                               .addressW = rhi::AddressMode::ClampToEdge,
                                               .debugName = "Mesh Clamp Sampler"});
        return m_whiteTexture && m_flatNormalTexture && m_missingTexture && m_blackCube && m_defaultSampler &&
               m_clampSampler && m_lights.init(device);
    }

    void MeshRenderer::shutdown()
    {
        for (auto& pipeline : m_pipelines)
            pipeline.reset();
        for (auto& pipeline : m_outlinePipelines)
            pipeline.reset();
        m_whiteTexture.reset();
        m_flatNormalTexture.reset();
        m_missingTexture.reset();
        m_blackCube.reset();
        m_defaultSampler.reset();
        m_clampSampler.reset();
        m_lights.shutdown();
    }

    void MeshRenderer::setLighting(Renderer& renderer, std::span<const GpuLight> lights, const LightingSettings& settings,
                                   const EnvironmentLighting* environment)
    {
        if (!renderer.isFrameActive())
            return;
        // beginFrame() waited until the GPU finished the frame that last used this slot, so its
        // buffer is free to overwrite.
        m_lightSlot = renderer.device().frameSlot();
        const uint32_t count = m_lights.write(m_lightSlot, lights);
        m_lightingConstants = makeLightingConstants(settings, count, environment);
        m_environment = environment && environment->complete() ? *environment : EnvironmentLighting{};
    }

    RenderStats MeshRenderer::draw(Renderer& renderer, const RenderList& list, Queue queue, DebugView debugView)
    {
        RenderStats stats;
        rhi::CommandList& commandList = renderer.commandList();
        const bool reversedZ = renderer.sceneCamera().reversedZ;
        const MaterialData defaultMaterial;
        commandList.setConstants(0, renderer.sceneViewConstants());
        commandList.setConstants(kLightingConstantsSlot, m_lightingConstants);
        if (rhi::Buffer* lights = m_lights.buffer(m_lightSlot))
            commandList.setBuffer(kLightBufferSlot, *lights);
        // Bound whether or not the environment lights this frame: Mesh.hlsl declares the slots.
        commandList.setTexture(kIrradianceSlot, m_environment.irradiance ? *m_environment.irradiance : *m_blackCube);
        commandList.setTexture(kPrefilteredSlot, m_environment.prefiltered ? *m_environment.prefiltered : *m_blackCube);
        commandList.setTexture(kBrdfLutSlot, m_environment.brdfLut ? *m_environment.brdfLut : *m_whiteTexture);
        commandList.setSampler(kEnvironmentSamplerSlot,
                               m_environment.sampler ? *m_environment.sampler : *m_clampSampler);

        // The state bound so far, so a draw only changes what differs from the one before it.
        // Bindings stay across pipeline changes; buffers need a pipeline set first only because the
        // vertex stride comes from its layout.
        rhi::Pipeline* pipeline = nullptr;
        const rhi::Buffer* vertexBuffer = nullptr;
        const rhi::Buffer* indexBuffer = nullptr;
        uint32_t transform = std::numeric_limits<uint32_t>::max();
        std::array<const rhi::Texture*, kMaterialTextureSlots> textures{};
        const rhi::Sampler* sampler = nullptr;
        const MaterialData* material = nullptr;
        const MaterialStyle* style = nullptr;
        const rhi::Texture* ramp = nullptr;

        const auto drawQueue = [&](std::span<const RenderItem> items, bool blend) {
            for (const RenderItem& item : items)
            {
                const Model& model = *item.model;
                const Primitive& primitive = model.primitives()[item.primitive];
                const PrimitiveDraw& draw = model.primitiveLods()[item.primitive].levels[item.lod];
                const RenderTransform& placement = list.transforms()[item.transform];
                const MaterialData& itemMaterial =
                    primitive.material >= 0 ? model.materials()[static_cast<size_t>(primitive.material)] : defaultMaterial;

                rhi::Pipeline* wanted = m_pipelines[PipelineKey{.reversedZ = reversedZ,
                                                                .doubleSided = itemMaterial.doubleSided,
                                                                .blend = blend,
                                                                .mirrored = placement.mirrored}
                                                        .index()]
                                            .get();
                if (wanted != pipeline)
                {
                    commandList.setPipeline(*wanted);
                    pipeline = wanted;
                    ++stats.pipelineChanges;
                }

                GeometryPool& geometry = model.geometry();
                rhi::Buffer& vertices = geometry.vertexBuffer(model.geometryBlock());
                if (&vertices != vertexBuffer)
                {
                    commandList.setVertexBuffer(vertices);
                    vertexBuffer = &vertices;
                    ++stats.bufferBinds;
                }
                rhi::Buffer& indices = geometry.indexBuffer(model.geometryBlock(), draw.indexFormat);
                if (&indices != indexBuffer)
                {
                    commandList.setIndexBuffer(indices, draw.indexFormat);
                    indexBuffer = &indices;
                    ++stats.bufferBinds;
                }

                if (item.transform != transform)
                {
                    commandList.setConstants(1, MeshDrawConstants{
                                                    .world = placement.world,
                                                    .normalMatrix = normalMatrixOf(placement.world),
                                                    .debugView = static_cast<uint32_t>(debugView),
                                                    .handedness = placement.mirrored ? -1.0f : 1.0f,
                                                });
                    transform = item.transform;
                    ++stats.constantUploads;
                }

                std::array<rhi::Texture*, kMaterialTextureSlots> itemTextures{
                    m_whiteTexture.get(), m_whiteTexture.get(), m_flatNormalTexture.get(), m_whiteTexture.get(),
                    m_whiteTexture.get()};
                rhi::Sampler* itemSampler = nullptr;
                if (itemMaterial.baseColorTexture >= 0)
                {
                    rhi::Texture* baseColor = model.texture(itemMaterial.baseColorTexture);
                    itemSampler = model.sampler(itemMaterial.baseColorTexture);
                    itemTextures[kBaseColorSlot] = baseColor && itemSampler ? baseColor : m_missingTexture.get();
                }
                // The first texture the material has decides the sampler when it has no base colour.
                const std::array<std::pair<uint32_t, int32_t>, 4> dataTextures{{
                    {kMetallicRoughnessSlot, itemMaterial.metallicRoughnessTexture},
                    {kNormalSlot, itemMaterial.normalTexture},
                    {kOcclusionSlot, itemMaterial.occlusionTexture},
                    {kEmissiveSlot, itemMaterial.emissiveTexture},
                }};
                for (const auto& [slot, index] : dataTextures)
                {
                    if (index < 0)
                        continue;
                    if (rhi::Texture* texture = model.texture(index))
                    {
                        itemTextures[slot] = texture;
                        if (!itemSampler)
                            itemSampler = model.sampler(index);
                    }
                }
                if (!itemSampler)
                    itemSampler = m_defaultSampler.get();
                for (uint32_t slot = 0; slot < kMaterialTextureSlots; ++slot)
                {
                    if (itemTextures[slot] != textures[slot])
                    {
                        commandList.setTexture(slot, *itemTextures[slot]);
                        textures[slot] = itemTextures[slot];
                        ++stats.textureBinds;
                    }
                }
                if (itemSampler != sampler)
                {
                    commandList.setSampler(0, *itemSampler);
                    sampler = itemSampler;
                }
                const bool toon = item.style && item.style->shading == ShadingModel::Toon;
                rhi::Texture* itemRamp = toon && item.style->toon.ramp ? item.style->toon.ramp : m_whiteTexture.get();
                if (itemRamp != ramp)
                {
                    commandList.setTexture(kToonRampSlot, *itemRamp);
                    ramp = itemRamp;
                    ++stats.textureBinds;
                }
                if (&itemMaterial != material || item.style != style)
                {
                    commandList.setConstants(2, materialConstants(itemMaterial, item.style));
                    material = &itemMaterial;
                    style = item.style;
                    ++stats.constantUploads;
                }

                commandList.drawIndexed(draw.indexCount, 1, draw.firstIndex, draw.baseVertex);
                ++stats.draws;
                ++stats.drawsPerLod[item.lod];
                stats.triangles += draw.indexCount / 3;
            }
        };

        const bool blendedQueue = queue == Queue::Blended;
        const std::span<const RenderItem> queueItems = blendedQueue ? list.blended() : list.opaque();
        if (!queueItems.empty())
        {
            const rhi::ScopedDebugGroup group(commandList, blendedQueue ? "Blended" : "Opaque");
            drawQueue(queueItems, blendedQueue);
        }
        return stats;
    }

    RenderStats MeshRenderer::drawOutlines(Renderer& renderer, const RenderList& list)
    {
        RenderStats stats;
        rhi::CommandList& commandList = renderer.commandList();
        const bool reversedZ = renderer.sceneCamera().reversedZ;
        const Extent2D viewport = renderer.sceneSize();
        bool begun = false;
        rhi::Pipeline* pipeline = nullptr;
        const rhi::Buffer* vertexBuffer = nullptr;
        const rhi::Buffer* indexBuffer = nullptr;
        uint32_t transform = std::numeric_limits<uint32_t>::max();
        const OutlineStyle* outline = nullptr;

        for (const RenderItem& item : list.opaque())
        {
            if (!item.style || !item.style->outline.enabled || item.style->outline.width <= 0.0f)
                continue;
            if (!begun)
            {
                commandList.pushDebugGroup("Toon Outlines");
                commandList.setConstants(0, renderer.sceneViewConstants());
                begun = true;
            }
            const Model& model = *item.model;
            const PrimitiveDraw& draw = model.primitiveLods()[item.primitive].levels[item.lod];
            const RenderTransform& placement = list.transforms()[item.transform];

            rhi::Pipeline* wanted = m_outlinePipelines[size_t{reversedZ} | size_t{placement.mirrored} << 1].get();
            if (wanted != pipeline)
            {
                commandList.setPipeline(*wanted);
                pipeline = wanted;
                ++stats.pipelineChanges;
            }
            GeometryPool& geometry = model.geometry();
            rhi::Buffer& vertices = geometry.vertexBuffer(model.geometryBlock());
            if (&vertices != vertexBuffer)
            {
                commandList.setVertexBuffer(vertices);
                vertexBuffer = &vertices;
                ++stats.bufferBinds;
            }
            rhi::Buffer& indices = geometry.indexBuffer(model.geometryBlock(), draw.indexFormat);
            if (&indices != indexBuffer)
            {
                commandList.setIndexBuffer(indices, draw.indexFormat);
                indexBuffer = &indices;
                ++stats.bufferBinds;
            }
            if (item.transform != transform)
            {
                commandList.setConstants(1, MeshDrawConstants{
                                                .world = placement.world,
                                                .normalMatrix = normalMatrixOf(placement.world),
                                                .handedness = placement.mirrored ? -1.0f : 1.0f,
                                            });
                transform = item.transform;
                ++stats.constantUploads;
            }
            if (&item.style->outline != outline)
            {
                commandList.setConstants(2, OutlineConstants{
                                                .color = item.style->outline.color,
                                                .width = item.style->outline.width,
                                                .viewportSize = {static_cast<float>(viewport.width),
                                                                 static_cast<float>(viewport.height)},
                                            });
                outline = &item.style->outline;
                ++stats.constantUploads;
            }
            commandList.drawIndexed(draw.indexCount, 1, draw.firstIndex, draw.baseVertex);
            ++stats.draws;
            stats.triangles += draw.indexCount / 3;
        }
        if (begun)
            commandList.popDebugGroup();
        return stats;
    }
}

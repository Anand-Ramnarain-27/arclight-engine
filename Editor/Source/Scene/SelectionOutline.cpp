#include "Scene/SelectionOutline.h"

#include "RHI/CommandList.h"
#include "RHI/Device.h"
#include "RHI/Pipeline.h"
#include "RHI/Shader.h"
#include "RHI/Texture.h"
#include "Renderer/GeometryPool.h"
#include "Renderer/Model.h"
#include "Renderer/Renderer.h"
#include "Renderer/ShaderLibrary.h"
#include "Scene/GameObject.h"
#include "Scene/MeshComponent.h"

#include <imgui.h>

#include <algorithm>
#include <cmath>
#include <cstddef>

namespace arc
{
    namespace
    {
        // Mirrors MaskDrawConstants in SelectionMask.hlsl.
        struct MaskDrawConstants
        {
            Mat4 world;
        };
        static_assert(sizeof(MaskDrawConstants) == 64);

        // Mirrors OutlineConstants in Outline.hlsl: a float4 row, then an int padded to a full row.
        struct OutlineConstants
        {
            Vec4 color;
            int32_t radius = 2;
            int32_t padding[3]{};
        };
        static_assert(offsetof(OutlineConstants, radius) == 16);
        static_assert(sizeof(OutlineConstants) == 32);

        constexpr rhi::Format kMaskFormat = rhi::Format::RGBA8_UNORM;
        constexpr int kMaxThickness = 4;   // kMaxRadius in Outline.hlsl.
    }

    struct SelectionOutline::MaskItem
    {
        const Model* model = nullptr;
        uint32_t mesh = 0;
        Mat4 world;
    };

    SelectionOutline::SelectionOutline() = default;

    // Defined here, where the RHI types are complete, for the unique_ptr destructors.
    SelectionOutline::~SelectionOutline() = default;

    bool SelectionOutline::init(Renderer& renderer)
    {
        const ShaderLibrary& shaders = renderer.shaders();
        const auto maskVertex = shaders.load("SelectionMask", rhi::ShaderStage::Vertex, "VSMain");
        const auto maskPixel = shaders.load("SelectionMask", rhi::ShaderStage::Pixel, "PSMain");
        const auto outlineVertex = shaders.load("Outline", rhi::ShaderStage::Vertex, "VSMain");
        const auto outlinePixel = shaders.load("Outline", rhi::ShaderStage::Pixel, "PSMain");
        if (!maskVertex || !maskPixel || !outlineVertex || !outlinePixel)
            return false;

        rhi::PipelineDesc mask;
        mask.vertexShader = maskVertex.get();
        mask.pixelShader = maskPixel.get();
        // Only the position is read, but the stride is the whole vertex the model's buffer holds.
        mask.vertexLayout = {
            .stride = sizeof(MeshVertex),
            .attributes = {{.semantic = "POSITION", .format = rhi::VertexFormat::Float3, .offset = offsetof(MeshVertex, position)}},
        };
        mask.colorFormat = kMaskFormat;
        // Both faces: a silhouette has no inside, and mirrored instances need no second pipeline.
        mask.cullMode = rhi::CullMode::None;
        mask.debugName = "Selection Mask";
        m_maskPipeline = renderer.device().createPipeline(mask);

        rhi::PipelineDesc outline;
        outline.vertexShader = outlineVertex.get();
        outline.pixelShader = outlinePixel.get();
        outline.colorFormat = Renderer::sceneFormat();
        outline.depthFormat = Renderer::sceneDepthFormat();
        // No depth test: the outline shows through whatever covers the selection, as in most editors.
        outline.blend = rhi::BlendMode::AlphaBlend;
        outline.cullMode = rhi::CullMode::None;
        outline.debugName = "Selection Outline";
        m_outlinePipeline = renderer.device().createPipeline(outline);
        return m_maskPipeline && m_outlinePipeline;
    }

    void SelectionOutline::shutdown()
    {
        m_maskPipeline.reset();
        m_outlinePipeline.reset();
        m_mask.reset();
    }

    void SelectionOutline::prepare(Renderer& renderer)
    {
        const Extent2D size = renderer.sceneSize();
        if (size.width == 0 || size.height == 0)
            return;
        if (m_mask && m_mask->desc().size.width == size.width && m_mask->desc().size.height == size.height)
            return;
        // Frames in flight may still read the old mask; the device keeps it until they finish.
        m_mask = renderer.device().createTexture({
            .size = size,
            .format = kMaskFormat,
            .usage = rhi::TextureUsage::RenderTarget | rhi::TextureUsage::Sampled,
            .debugName = "Selection Mask",
        });
    }

    void SelectionOutline::render(Renderer& renderer, const GameObject* selected)
    {
        if (!selected || !m_mask || !m_maskPipeline || !m_outlinePipeline || !renderer.isFrameActive())
            return;

        m_items.clear();
        const auto collect = [this](const GameObject& object, const auto& self) -> void {
            if (!object.isActiveSelf())
                return;
            for (const std::unique_ptr<Component>& component : object.components())
            {
                if (!component->enabled || &component->type() != &MeshComponent::staticType())
                    continue;
                const auto& mesh = static_cast<const MeshComponent&>(*component);
                if (mesh.hasMesh())
                    m_items.push_back({mesh.model().get(), mesh.meshIndex(), object.transform().worldMatrix()});
            }
            for (const GameObject* child : object.children())
                self(*child, self);
        };
        // An object under an inactive parent is not drawn, so it gets no outline either.
        if (!selected->isActiveInHierarchy())
            return;
        collect(*selected, collect);
        if (m_items.empty())
            return;

        rhi::CommandList& commandList = renderer.commandList();
        rhi::RenderPassDesc pass;
        pass.color = {.texture = m_mask.get(), .load = rhi::LoadOp::Clear, .store = rhi::StoreOp::Store, .clearColor = {}};
        pass.debugName = "Selection Mask";
        commandList.beginRenderPass(pass);
        commandList.setPipeline(*m_maskPipeline);
        commandList.setConstants(0, renderer.sceneViewConstants());
        const rhi::Buffer* vertexBuffer = nullptr;
        const rhi::Buffer* indexBuffer = nullptr;
        for (const MaskItem& item : m_items)
        {
            commandList.setConstants(1, MaskDrawConstants{item.world});
            GeometryPool& geometry = item.model->geometry();
            rhi::Buffer& vertices = geometry.vertexBuffer(item.model->geometryBlock());
            if (&vertices != vertexBuffer)
            {
                commandList.setVertexBuffer(vertices);
                vertexBuffer = &vertices;
            }
            const MeshData& mesh = item.model->meshes()[item.mesh];
            for (uint32_t i = 0; i < mesh.primitiveCount; ++i)
            {
                const PrimitiveDraw& draw = item.model->primitiveLods()[mesh.firstPrimitive + i].levels[0];
                rhi::Buffer& indices = geometry.indexBuffer(item.model->geometryBlock(), draw.indexFormat);
                if (&indices != indexBuffer)
                {
                    commandList.setIndexBuffer(indices, draw.indexFormat);
                    indexBuffer = &indices;
                }
                commandList.drawIndexed(draw.indexCount, 1, draw.firstIndex, draw.baseVertex);
            }
        }
        commandList.endRenderPass();
        commandList.barrier(*m_mask, rhi::ResourceState::ShaderRead);

        if (!renderer.beginScenePass("Selection Outline"))
            return;
        // The mask is in pixels, so the outline's width is too; scaling by the display scale keeps
        // it the same width in points on high-DPI screens.
        const float scale = ImGui::GetIO().DisplayFramebufferScale.y;
        const int radius = std::clamp(static_cast<int>(std::lround(static_cast<float>(thickness) * scale)), 1, kMaxThickness);
        commandList.setPipeline(*m_outlinePipeline);
        commandList.setTexture(0, *m_mask);
        commandList.setConstants(1, OutlineConstants{.color = color, .radius = radius});
        commandList.draw(3);
        renderer.endScenePass();
    }
}

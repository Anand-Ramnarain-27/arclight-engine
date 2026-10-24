#include "Scene/EditorGrid.h"

#include "RHI/CommandList.h"
#include "RHI/Device.h"
#include "RHI/Pipeline.h"
#include "RHI/Shader.h"
#include "Renderer/Renderer.h"
#include "Renderer/ShaderLibrary.h"

#include <cstddef>

namespace arc
{
    namespace
    {
        // Mirrors GridConstants in Grid.hlsl: four floats, one 16-byte row.
        struct GridConstants
        {
            float cellSize = 1.0f;
            float majorEvery = 10.0f;
            float fadeDistance = 150.0f;
            float opacity = 1.0f;
        };
        static_assert(offsetof(GridConstants, opacity) == 12);
        static_assert(sizeof(GridConstants) == 16);
    }

    EditorGrid::EditorGrid() = default;

    // Defined here, where Pipeline is complete, for the unique_ptr destructors.
    EditorGrid::~EditorGrid() = default;

    bool EditorGrid::init(Renderer& renderer)
    {
        const auto vertexShader = renderer.shaders().load("Grid", rhi::ShaderStage::Vertex, "VSMain");
        const auto pixelShader = renderer.shaders().load("Grid", rhi::ShaderStage::Pixel, "PSMain");
        if (!vertexShader || !pixelShader)
            return false;

        for (const bool reversedZ : {false, true})
        {
            Camera camera;
            camera.reversedZ = reversedZ;

            rhi::PipelineDesc desc;
            desc.vertexShader = vertexShader.get();
            desc.pixelShader = pixelShader.get();
            desc.colorFormat = Renderer::displayFormat();
            desc.depthFormat = Renderer::sceneDepthFormat();
            desc.depth = {.test = true, .write = false, .compare = camera.depthCompare()};
            desc.blend = rhi::BlendMode::AlphaBlend;
            // The full-screen triangle faces the camera, but the grid is visible from below too, and
            // nothing is gained by naming the triangle's winding.
            desc.cullMode = rhi::CullMode::None;
            desc.debugName = reversedZ ? "Grid (Reversed-Z)" : "Grid";
            m_pipelines[reversedZ] = renderer.device().createPipeline(desc);
            if (!m_pipelines[reversedZ])
                return false;
        }
        return true;
    }

    void EditorGrid::shutdown()
    {
        for (auto& pipeline : m_pipelines)
            pipeline.reset();
    }

    void EditorGrid::render(Renderer& renderer)
    {
        rhi::Pipeline* pipeline = m_pipelines[renderer.sceneCamera().reversedZ].get();
        if (!visible || !pipeline || !renderer.isFrameActive() || !renderer.beginOverlayPass("Grid"))
            return;

        rhi::CommandList& commandList = renderer.commandList();
        commandList.setPipeline(*pipeline);
        commandList.setConstants(0, renderer.sceneViewConstants());
        commandList.setConstants(1, GridConstants{cellSize, majorEvery, fadeDistance, opacity});
        commandList.draw(3);
        renderer.endOverlayPass();
    }
}

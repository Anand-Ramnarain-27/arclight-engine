#include "Scene/TriangleModule.h"

#include "Application/Application.h"
#include "Core/Log.h"
#include "Math/Vector.h"
#include "RHI/Buffer.h"
#include "RHI/BufferUtils.h"
#include "RHI/CommandList.h"
#include "RHI/Device.h"
#include "RHI/Pipeline.h"
#include "RHI/Shader.h"
#include "RHI/Swapchain.h"
#include "Renderer/ShaderLibrary.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

namespace arc
{
    namespace
    {
        // Matches VSInput in Triangle.hlsl, member for member.
        struct Vertex
        {
            Vec3 position;   // Clip space.
            Vec3 color;      // Linear RGB.
        };
        static_assert(sizeof(Vertex) == 24, "Vertex must be tightly packed to match the vertex layout");

        // Counter-clockwise when viewed from the front, the engine's front-face convention.
        constexpr std::array<Vertex, 3> kVertices = {{
            {{0.0f, 0.6f, 0.0f}, {1.0f, 0.25f, 0.2f}},
            {{-0.55f, -0.5f, 0.0f}, {0.2f, 0.85f, 0.35f}},
            {{0.55f, -0.5f, 0.0f}, {0.25f, 0.45f, 1.0f}},
        }};

        constexpr std::array<uint16_t, 3> kIndices = {0, 1, 2};
    }

    TriangleModule::TriangleModule(Application& app) : Module(app) {}

    // Defined here, where the RHI types are complete, for the unique_ptr destructors.
    TriangleModule::~TriangleModule() = default;

    bool TriangleModule::init()
    {
        Renderer& renderer = app().renderer();
        rhi::Device& device = renderer.device();

        const auto vertexShader = renderer.shaders().load("Triangle", rhi::ShaderStage::Vertex, "VSMain");
        const auto pixelShader = renderer.shaders().load("Triangle", rhi::ShaderStage::Pixel, "PSMain");
        if (!vertexShader || !pixelShader)
            return false;

        rhi::PipelineDesc pipelineDesc;
        pipelineDesc.vertexShader = vertexShader.get();
        pipelineDesc.pixelShader = pixelShader.get();
        pipelineDesc.vertexLayout = {
            .stride = sizeof(Vertex),
            .attributes = {
                {.semantic = "POSITION", .format = rhi::VertexFormat::Float3, .offset = offsetof(Vertex, position)},
                {.semantic = "COLOR", .format = rhi::VertexFormat::Float3, .offset = offsetof(Vertex, color)},
            },
        };
        pipelineDesc.colorFormat = renderer.swapchain().desc().format;
        pipelineDesc.debugName = "Triangle";
        // The pipeline keeps everything it needs, so the shaders can be released afterwards.
        m_pipeline = device.createPipeline(pipelineDesc);
        if (!m_pipeline)
            return false;

        m_vertexBuffer = device.createBuffer({
            .size = sizeof(kVertices),
            .usage = rhi::BufferUsage::Vertex,
            .memory = rhi::MemoryType::GpuOnly,
            .debugName = "Triangle Vertices",
        });
        m_indexBuffer = device.createBuffer({
            .size = sizeof(kIndices),
            .usage = rhi::BufferUsage::Index,
            .memory = rhi::MemoryType::GpuOnly,
            .debugName = "Triangle Indices",
        });
        if (!m_vertexBuffer || !m_indexBuffer)
            return false;

        if (!rhi::uploadBuffer(device, *m_vertexBuffer, std::as_bytes(std::span(kVertices))) ||
            !rhi::uploadBuffer(device, *m_indexBuffer, std::as_bytes(std::span(kIndices))))
            return false;

        ARC_LOG_INFO("Triangle pipeline created");
        return true;
    }

    void TriangleModule::render()
    {
        Renderer& renderer = app().renderer();
        if (!renderer.isFrameActive() || !renderer.beginBackbufferPass("Triangle"))
            return;

        rhi::CommandList& commandList = renderer.commandList();
        commandList.setPipeline(*m_pipeline);
        commandList.setVertexBuffer(*m_vertexBuffer);
        commandList.setIndexBuffer(*m_indexBuffer, rhi::IndexFormat::UInt16);
        commandList.drawIndexed(static_cast<uint32_t>(kIndices.size()));

        renderer.endBackbufferPass();
    }

    void TriangleModule::shutdown()
    {
        // The Application waits for the GPU before shutting modules down, so nothing in flight
        // still uses these.
        m_pipeline.reset();
        m_vertexBuffer.reset();
        m_indexBuffer.reset();
    }
}

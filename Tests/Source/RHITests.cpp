#include "RHI/Backends.h"
#include "RHI/Buffer.h"
#include "RHI/BufferUtils.h"
#include "RHI/CommandList.h"
#include "RHI/Device.h"
#include "RHI/Pipeline.h"
#include "RHI/Shader.h"
#include "RHI/ShaderUtils.h"
#include "RHI/Texture.h"

#include <doctest/doctest.h>

#include <cstddef>
#include <vector>

using namespace arc;
using namespace arc::rhi;

TEST_CASE("Buffers start in the state their memory type requires")
{
    auto device = createNullDevice({});
    REQUIRE(device);

    auto gpuOnly = device->createBuffer({.size = 256, .memory = MemoryType::GpuOnly, .debugName = "GpuOnly"});
    auto upload = device->createBuffer({.size = 256, .memory = MemoryType::Upload, .debugName = "Upload"});
    auto readback = device->createBuffer({.size = 256, .memory = MemoryType::Readback, .debugName = "Readback"});

    CHECK(gpuOnly->state() == ResourceState::Common);
    CHECK(gpuOnly->mappedData() == nullptr);
    CHECK(upload->state() == ResourceState::CopySource);
    CHECK(upload->mappedData() != nullptr);
    CHECK(readback->state() == ResourceState::CopyDest);
    CHECK(readback->mappedData() != nullptr);
}

TEST_CASE("Buffer states return to Common when a command list is submitted")
{
    auto device = createNullDevice({});
    REQUIRE(device);

    auto buffer = device->createBuffer({.size = 64, .usage = BufferUsage::Vertex, .debugName = "Decay"});
    device->submitImmediate([&](CommandList& commandList) {
        commandList.barrier(*buffer, ResourceState::CopyDest);
        CHECK(buffer->state() == ResourceState::CopyDest);
        commandList.barrier(*buffer, ResourceState::VertexBuffer);
        CHECK(buffer->state() == ResourceState::VertexBuffer);
    });
    CHECK(buffer->state() == ResourceState::Common);
}

TEST_CASE("Upload then readback returns the same bytes")
{
    auto device = createNullDevice({});
    REQUIRE(device);
    CHECK(verifyBufferRoundTrip(*device, 4096));
}

TEST_CASE("Partial upload writes only the requested range")
{
    auto device = createNullDevice({});
    REQUIRE(device);

    auto buffer = device->createBuffer({.size = 16, .usage = BufferUsage::Index, .debugName = "Partial"});
    const std::byte zeros[16]{};
    REQUIRE(uploadBuffer(*device, *buffer, zeros));

    const std::byte ones[4]{std::byte{1}, std::byte{1}, std::byte{1}, std::byte{1}};
    REQUIRE(uploadBuffer(*device, *buffer, ones, 8));

    const std::vector<std::byte> bytes = readbackBuffer(*device, *buffer, 0, 16);
    REQUIRE(bytes.size() == 16);
    for (size_t i = 0; i < 16; ++i)
        CHECK(bytes[i] == std::byte{static_cast<unsigned char>(i >= 8 && i < 12 ? 1 : 0)});
}

TEST_CASE("Shader creation rejects bytes that are not DXIL")
{
    auto device = createNullDevice({});
    REQUIRE(device);

    const std::byte notDxil[8]{std::byte{'M'}, std::byte{'T'}, std::byte{'L'}, std::byte{'B'}};
    CHECK_FALSE(looksLikeDxil(notDxil));
    CHECK(device->createShader({.stage = ShaderStage::Vertex, .bytecode = notDxil, .entryPoint = "VSMain", .debugName = "bad"}) == nullptr);

    const std::byte dxilHeader[8]{std::byte{'D'}, std::byte{'X'}, std::byte{'B'}, std::byte{'C'}};
    CHECK(looksLikeDxil(dxilHeader));
    auto shader = device->createShader({.stage = ShaderStage::Pixel, .bytecode = dxilHeader, .entryPoint = "PSMain", .debugName = "good"});
    REQUIRE(shader);
    CHECK(shader->stage() == ShaderStage::Pixel);
    CHECK(shader->entryPoint() == "PSMain");
}

TEST_CASE("A pipeline keeps the description it was created from")
{
    auto device = createNullDevice({});
    REQUIRE(device);

    const std::byte dxilHeader[8]{std::byte{'D'}, std::byte{'X'}, std::byte{'B'}, std::byte{'C'}};
    auto vertexShader = device->createShader({.stage = ShaderStage::Vertex, .bytecode = dxilHeader, .entryPoint = "VSMain"});
    auto pixelShader = device->createShader({.stage = ShaderStage::Pixel, .bytecode = dxilHeader, .entryPoint = "PSMain"});
    REQUIRE(vertexShader);
    REQUIRE(pixelShader);

    PipelineDesc pipelineDesc;
    pipelineDesc.vertexShader = vertexShader.get();
    pipelineDesc.pixelShader = pixelShader.get();
    pipelineDesc.vertexLayout = {
        .stride = 24,
        .attributes = {
            {.semantic = "POSITION", .format = VertexFormat::Float3, .offset = 0},
            {.semantic = "COLOR", .format = VertexFormat::Float3, .offset = 12},
        },
    };
    pipelineDesc.debugName = "Test";

    auto pipeline = device->createPipeline(pipelineDesc);
    REQUIRE(pipeline);
    CHECK(pipeline->desc().vertexLayout.stride == 24);
    CHECK(pipeline->desc().vertexLayout.attributes.size() == 2);
    CHECK(pipeline->desc().vertexLayout.attributes[1].semantic == "COLOR");
    CHECK(pipeline->desc().frontCounterClockwise);
}

TEST_CASE("A depth target goes to DepthWrite when a render pass uses it")
{
    auto device = createNullDevice({});
    REQUIRE(device);

    CHECK(isDepthFormat(Format::D32_FLOAT));
    CHECK_FALSE(isDepthFormat(Format::RGBA8_UNORM));
    CHECK(bytesPerPixel(Format::D32_FLOAT) == 4);

    auto color = device->createTexture(
        {.size = {8, 8}, .format = Format::RGBA8_UNORM, .usage = TextureUsage::RenderTarget, .debugName = "Color"});
    auto depth = device->createTexture(
        {.size = {8, 8}, .format = Format::D32_FLOAT, .usage = TextureUsage::DepthStencil, .debugName = "Depth"});
    REQUIRE(color);
    REQUIRE(depth);
    CHECK(depth->state() == ResourceState::Common);

    const std::byte dxilHeader[8]{std::byte{'D'}, std::byte{'X'}, std::byte{'B'}, std::byte{'C'}};
    auto vertexShader = device->createShader({.stage = ShaderStage::Vertex, .bytecode = dxilHeader, .entryPoint = "VSMain"});
    auto pixelShader = device->createShader({.stage = ShaderStage::Pixel, .bytecode = dxilHeader, .entryPoint = "PSMain"});
    REQUIRE(vertexShader);
    REQUIRE(pixelShader);

    // No vertex attributes: the pipeline draws without a vertex buffer.
    PipelineDesc pipelineDesc;
    pipelineDesc.vertexShader = vertexShader.get();
    pipelineDesc.pixelShader = pixelShader.get();
    pipelineDesc.colorFormat = Format::RGBA8_UNORM;
    pipelineDesc.depthFormat = Format::D32_FLOAT;
    pipelineDesc.depth = {.test = true, .write = true, .compare = CompareOp::GreaterEqual};
    pipelineDesc.debugName = "Depth Test";
    auto pipeline = device->createPipeline(pipelineDesc);
    REQUIRE(pipeline);

    device->submitImmediate([&](CommandList& commandList) {
        RenderPassDesc pass;
        pass.color.texture = color.get();
        pass.depth.texture = depth.get();
        pass.depth.clearDepth = 0.0f;
        commandList.beginRenderPass(pass);
        commandList.setPipeline(*pipeline);
        commandList.draw(3);
        commandList.endRenderPass();
    });
    CHECK(color->state() == ResourceState::RenderTarget);
    CHECK(depth->state() == ResourceState::DepthWrite);
}

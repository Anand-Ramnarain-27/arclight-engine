#include "RHI/Backends.h"
#include "RHI/Buffer.h"
#include "RHI/BufferUtils.h"
#include "RHI/CommandList.h"
#include "RHI/Device.h"
#include "RHI/Shader.h"
#include "RHI/ShaderUtils.h"

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

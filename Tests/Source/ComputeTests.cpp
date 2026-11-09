#include "RHI/Backends.h"
#include "RHI/Buffer.h"
#include "RHI/CommandList.h"
#include "RHI/Device.h"
#include "RHI/Pipeline.h"
#include "RHI/Shader.h"
#include "RHI/Texture.h"

#include <doctest/doctest.h>

#include <cstddef>
#include <memory>
#include <string>

using namespace arc;
using namespace arc::rhi;

namespace
{
    std::unique_ptr<Shader> fakeComputeShader(Device& device)
    {
        const std::byte dxilHeader[8]{std::byte{'D'}, std::byte{'X'}, std::byte{'B'}, std::byte{'C'}};
        return device.createShader({.stage = ShaderStage::Compute, .bytecode = dxilHeader, .entryPoint = "CSMain"});
    }
}

TEST_CASE("Thread group counts round up to cover every item")
{
    CHECK(groupCount(1, 16) == 1);
    CHECK(groupCount(16, 16) == 1);
    CHECK(groupCount(17, 16) == 2);
    CHECK(groupCount(1920, 16) == 120);
    CHECK(groupCount(1080, 16) == 68);
    CHECK(ThreadGroupSize{16, 16, 1}.count() == 256);
}

TEST_CASE("A compute pipeline keeps its shader stage and thread group size")
{
    auto device = createNullDevice({});
    REQUIRE(device);
    auto shader = fakeComputeShader(*device);
    REQUIRE(shader);
    CHECK(shader->stage() == ShaderStage::Compute);
    CHECK(std::string(shaderStageName(ShaderStage::Compute)) == "compute");

    auto pipeline = device->createComputePipeline(
        {.computeShader = shader.get(), .threadGroupSize = {8, 8, 1}, .debugName = "Test Compute"});
    REQUIRE(pipeline);
    CHECK(pipeline->desc().threadGroupSize.x == 8);
    CHECK(pipeline->desc().threadGroupSize.count() == 64);
    CHECK(pipeline->desc().debugName == "Test Compute");
}

TEST_CASE("Storage buffers and textures are recorded in compute passes in the UnorderedAccess state")
{
    auto device = createNullDevice({});
    REQUIRE(device);
    auto shader = fakeComputeShader(*device);
    auto pipeline = device->createComputePipeline({.computeShader = shader.get(), .threadGroupSize = {8, 8, 1}});
    auto values = device->createBuffer(
        {.size = 256 * 4, .usage = BufferUsage::Storage | BufferUsage::Structured, .stride = 4, .debugName = "Values"});
    auto image = device->createTexture({.size = {32, 32},
                                        .format = Format::RGBA16_FLOAT,
                                        .mipLevels = 2,
                                        .usage = TextureUsage::Storage | TextureUsage::Sampled,
                                        .debugName = "Image"});
    auto source = device->createTexture({.size = {32, 32}, .format = Format::RGBA8_UNORM, .debugName = "Source"});
    REQUIRE(pipeline);
    REQUIRE(values);
    REQUIRE(image);
    REQUIRE(source);
    CHECK(std::string(resourceStateName(ResourceState::UnorderedAccess)) == "UnorderedAccess");

    device->submitImmediate([&](CommandList& commandList) {
        commandList.barrier(*source, ResourceState::ShaderRead);
        commandList.barrier(*values, ResourceState::UnorderedAccess);
        commandList.barrier(*image, ResourceState::UnorderedAccess);
        commandList.beginComputePass({.debugName = "Test"});
        commandList.setComputePipeline(*pipeline);
        commandList.setTexture(0, *source);
        commandList.setStorageBuffer(0, *values);
        commandList.setStorageTexture(1, *image, 1);
        commandList.setConstants(0, uint32_t{7});
        commandList.dispatch(groupCount(16, 8), groupCount(16, 8));
        commandList.storageBarrier();
        commandList.dispatch(1);
        commandList.endComputePass();

        // After the pass, the results are read as ordinary shader resources.
        CHECK(values->state() == ResourceState::UnorderedAccess);
        commandList.barrier(*values, ResourceState::ShaderRead);
        commandList.barrier(*image, ResourceState::ShaderRead);
    });
    // Buffers decay to Common at the end of a submission; textures keep their state.
    CHECK(values->state() == ResourceState::Common);
    CHECK(image->state() == ResourceState::ShaderRead);
}

TEST_CASE("Adapter info starts with an unknown wave width on the Null backend")
{
    auto device = createNullDevice({});
    REQUIRE(device);
    CHECK(device->adapterInfo().waveLaneCountMin == 0);
    CHECK(device->adapterInfo().waveLaneCountMax == 0);
}

#include "RHI/Backends.h"
#include "RHI/Buffer.h"
#include "RHI/CommandList.h"
#include "RHI/Device.h"
#include "RHI/Texture.h"
#include "RHI/UploadRing.h"

#include <doctest/doctest.h>

#include <algorithm>
#include <cstddef>

using namespace arc;
using namespace arc::rhi;

TEST_CASE("Upload ring allocations are aligned and stay inside their region")
{
    auto device = createNullDevice({});
    REQUIRE(device);

    UploadRing ring;
    REQUIRE(ring.init(*device, 4096, "Test Ring"));

    const UploadAllocation first = ring.allocate(0, 100, kConstantBufferAlignment);
    const UploadAllocation second = ring.allocate(0, 4, kConstantBufferAlignment);
    REQUIRE(first);
    REQUIRE(second);
    CHECK(first.offset % kConstantBufferAlignment == 0);
    CHECK(second.offset == first.offset + kConstantBufferAlignment);
    CHECK(ring.used(0) == kConstantBufferAlignment + 4);

    // Each region starts after the zero block and the regions before it.
    const UploadAllocation other = ring.allocate(1, 16, kConstantBufferAlignment);
    REQUIRE(other);
    CHECK(other.offset == first.offset + ring.regionSize());
}

TEST_CASE("A full upload ring region refuses allocations until it is reset")
{
    auto device = createNullDevice({});
    REQUIRE(device);

    UploadRing ring;
    REQUIRE(ring.init(*device, 1024, "Small Ring"));

    for (int i = 0; i < 4; ++i)
        CHECK(ring.allocate(0, 256, kConstantBufferAlignment));
    CHECK_FALSE(ring.allocate(0, 1, kConstantBufferAlignment));
    CHECK(ring.allocate(1, 256, kConstantBufferAlignment));

    ring.reset(0);
    CHECK(ring.allocate(0, 256, kConstantBufferAlignment));
}

TEST_CASE("The zero block reads as zero and is never handed out")
{
    auto device = createNullDevice({});
    REQUIRE(device);

    UploadRing ring;
    REQUIRE(ring.init(*device, 1024, "Zero Ring"));

    const UploadAllocation zeros = ring.zeroBlock();
    const auto* bytes = static_cast<const std::byte*>(zeros.buffer->mappedData()) + zeros.offset;
    CHECK(std::all_of(bytes, bytes + kMaxConstantBufferSize, [](std::byte b) { return b == std::byte{0}; }));

    const UploadAllocation allocation = ring.allocate(0, 16, 16);
    CHECK(allocation.offset >= zeros.offset + kMaxConstantBufferSize);
}

TEST_CASE("setConstants works inside a render pass of an immediate submission")
{
    auto device = createNullDevice({});
    REQUIRE(device);

    // The Null backend draws nothing; this checks the command list has an upload ring and that
    // its asserts accept the calls.
    struct Constants
    {
        float values[4];
    };
    auto target = device->createTexture(
        {.size = {4, 4}, .format = Format::RGBA8_UNORM, .usage = TextureUsage::RenderTarget, .debugName = "Target"});
    REQUIRE(target);
    device->submitImmediate([&](CommandList& commandList) {
        commandList.beginRenderPass({.color = {.texture = target.get()}});
        commandList.setConstants(0, Constants{{1.0f, 2.0f, 3.0f, 4.0f}});
        commandList.setConstants(1, Constants{{5.0f, 6.0f, 7.0f, 8.0f}});
        commandList.endRenderPass();
    });
}

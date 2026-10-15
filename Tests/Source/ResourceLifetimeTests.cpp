#include "RHI/Backends.h"
#include "RHI/Buffer.h"
#include "RHI/DeferredRelease.h"
#include "RHI/Device.h"
#include "RHI/Sampler.h"
#include "RHI/Texture.h"

#include <doctest/doctest.h>

#include <memory>

using namespace arc;
using namespace arc::rhi;

namespace
{
    // Counts how many instances are alive, to observe when the queue destroys them.
    struct Tracked
    {
        explicit Tracked(int& counter) : alive(&counter) { ++*alive; }
        Tracked(Tracked&& other) noexcept : alive(other.alive) { other.alive = nullptr; }
        Tracked& operator=(Tracked&& other) noexcept
        {
            reset();
            alive = other.alive;
            other.alive = nullptr;
            return *this;
        }
        ~Tracked() { reset(); }

        void reset()
        {
            if (alive)
                --*alive;
            alive = nullptr;
        }

        int* alive = nullptr;
    };
}

TEST_CASE("Retired objects live until the GPU passes their fence value")
{
    int alive = 0;
    DeferredReleaseQueue<Tracked> queue;
    queue.retire(Tracked(alive), 5);
    queue.retire(Tracked(alive), 7);
    CHECK(alive == 2);

    queue.release(4);
    CHECK(alive == 2);
    queue.release(5);
    CHECK(alive == 1);
    queue.release(9);
    CHECK(alive == 0);
    CHECK(queue.size() == 0);
}

TEST_CASE("Objects retired during a frame wait for that frame's fence value")
{
    int alive = 0;
    DeferredReleaseQueue<Tracked> queue;
    queue.retireAfterOpenFrame(Tracked(alive));

    // The frame has not been submitted, so no completed fence value can release it yet.
    queue.release(1000);
    CHECK(alive == 1);

    queue.stampOpenFrame(1001);
    queue.release(1000);
    CHECK(alive == 1);
    queue.release(1001);
    CHECK(alive == 0);
}

TEST_CASE("Resources are found by handle until they are destroyed")
{
    auto device = createNullDevice({});
    REQUIRE(device);
    // The device's own resources, such as its upload ring, are counted too.
    const Device::ResourceCounts before = device->resourceCounts();

    auto texture = device->createTexture({.size = {8, 8}, .format = Format::RGBA8_UNORM, .debugName = "Found"});
    auto buffer = device->createBuffer({.size = 16, .debugName = "Found"});
    auto sampler = device->createSampler({.debugName = "Found"});
    REQUIRE(texture);
    REQUIRE(buffer);
    REQUIRE(sampler);

    const TextureHandle textureHandle = texture->handle();
    const BufferHandle bufferHandle = buffer->handle();
    const SamplerHandle samplerHandle = sampler->handle();
    CHECK(device->find(textureHandle) == texture.get());
    CHECK(device->find(bufferHandle) == buffer.get());
    CHECK(device->find(samplerHandle) == sampler.get());

    const Device::ResourceCounts counts = device->resourceCounts();
    CHECK(counts.textures == before.textures + 1);
    CHECK(counts.buffers == before.buffers + 1);
    CHECK(counts.samplers == before.samplers + 1);

    texture.reset();
    buffer.reset();
    sampler.reset();
    CHECK(device->find(textureHandle) == nullptr);
    CHECK(device->find(bufferHandle) == nullptr);
    CHECK(device->find(samplerHandle) == nullptr);
    CHECK(device->resourceCounts().textures == before.textures);
    CHECK(device->resourceCounts().buffers == before.buffers);
}

TEST_CASE("A handle to a destroyed texture stays stale when its slot is reused")
{
    auto device = createNullDevice({});
    REQUIRE(device);

    auto first = device->createTexture({.size = {4, 4}, .format = Format::RGBA8_UNORM, .debugName = "First"});
    const TextureHandle stale = first->handle();
    first.reset();

    auto second = device->createTexture({.size = {4, 4}, .format = Format::RGBA8_UNORM, .debugName = "Second"});
    CHECK(second->handle().index == stale.index);
    CHECK(device->find(stale) == nullptr);
    CHECK(device->find(second->handle()) == second.get());
}

TEST_CASE("A resource that outlives its device is reported and does not touch the device")
{
    auto device = createNullDevice({});
    REQUIRE(device);
    auto leaked = device->createBuffer({.size = 16, .debugName = "Leaked on purpose"});
    device.reset();
    leaked.reset();
}

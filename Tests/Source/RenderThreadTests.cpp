#include "RHI/Backends.h"
#include "RHI/Buffer.h"
#include "RHI/Device.h"
#include "RHI/Swapchain.h"
#include "RHI/Texture.h"
#include "Renderer/GeometryPool.h"
#include "Renderer/RenderThread.h"

#include <doctest/doctest.h>

#include <atomic>
#include <memory>
#include <random>
#include <thread>
#include <vector>

using namespace arc;

namespace
{
    // The Null device's frames never touch their swapchain, so none is needed beyond the type.
    class HeadlessSwapchain final : public rhi::Swapchain
    {
    public:
        bool resize(Extent2D size) override
        {
            m_desc.size = size;
            return true;
        }
        rhi::Texture* acquireBackbuffer() override { return nullptr; }
        void setVSync(bool enabled) override { m_desc.vsync = enabled; }
    };
}

TEST_CASE("Work handed to the render thread runs there, one piece at a time")
{
    RenderThread thread("Test Render");
    CHECK_FALSE(thread.isCurrent());
    // Nothing submitted yet.
    thread.wait();

    std::thread::id ranOn;
    bool wasCurrent = false;
    thread.submit([&] {
        ranOn = std::this_thread::get_id();
        wasCurrent = thread.isCurrent();
    });
    thread.wait();
    CHECK(ranOn != std::this_thread::get_id());
    CHECK(wasCurrent);
    CHECK(thread.lastWorkMilliseconds() >= 0.0);
}

TEST_CASE("Each side sees what the other wrote across submit() and wait()")
{
    RenderThread thread("Test Render");
    // Plain integers: the hand-over is the only synchronisation, which ThreadSanitizer checks.
    int mainWrote = 0;
    int renderWrote = 0;
    for (int frame = 1; frame <= 500; ++frame)
    {
        mainWrote = frame;
        thread.submit([&, frame] {
            CHECK(mainWrote == frame);
            renderWrote = frame * 2;
        });
        thread.wait();
        CHECK(renderWrote == frame * 2);
    }
}

TEST_CASE("Work still running when the render thread is destroyed finishes first")
{
    std::atomic<bool> finished{false};
    {
        RenderThread thread("Test Render");
        thread.submit([&] {
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
            finished = true;
        });
    }
    CHECK(finished.load());
}

TEST_CASE("Resources come and go on one thread while another records frames")
{
    auto device = rhi::createNullDevice({});
    REQUIRE(device);
    HeadlessSwapchain swapchain;
    GeometryPool geometry(*device, {.vertexCapacity = 4096, .index16Capacity = 4096, .index32Capacity = 4096});

    // Created before the render thread starts, and only read by it.
    auto stable = device->createTexture({.size = {4, 4}, .format = rhi::Format::RGBA8_UNORM, .debugName = "Stable"});
    REQUIRE(stable);
    const rhi::TextureHandle stableHandle = stable->handle();

    RenderThread thread("Test Render");
    std::atomic<uint32_t> blocksSeen{0};
    std::mt19937 random(7);
    std::vector<std::unique_ptr<rhi::Texture>> textures;
    std::vector<std::unique_ptr<rhi::Buffer>> buffers;
    std::vector<GeometryAllocation> allocations;

    for (int frame = 0; frame < 200; ++frame)
    {
        thread.submit([&] {
            (void)device->beginFrame(swapchain);
            geometry.collect(device->frameNumber());
            CHECK(device->find(stableHandle) == stable.get());
            const rhi::Device::ResourceCounts counts = device->resourceCounts();
            CHECK(counts.textures >= 1);
            const uint32_t blocks = geometry.stats().blocks;
            for (uint32_t block = 0; block < blocks; ++block)
                CHECK(geometry.vertexBuffer(block).desc().size > 0);
            blocksSeen.store(blocks, std::memory_order_relaxed);
            device->endFrame(swapchain);
        });

        // Meanwhile, as the main thread does while models load and objects are deleted.
        for (int i = 0; i < 8; ++i)
        {
            if (random() % 2 == 0 || textures.empty())
                textures.push_back(device->createTexture(
                    {.size = {4, 4}, .format = rhi::Format::RGBA8_UNORM, .debugName = "Churn"}));
            else
                textures.erase(textures.begin() + static_cast<std::ptrdiff_t>(random() % textures.size()));
            if (random() % 2 == 0 || buffers.empty())
                buffers.push_back(device->createBuffer({.size = 256, .usage = rhi::BufferUsage::Vertex, .debugName = "Churn"}));
            else
                buffers.erase(buffers.begin() + static_cast<std::ptrdiff_t>(random() % buffers.size()));
            if (random() % 3 != 0 || allocations.empty())
            {
                if (const std::optional<GeometryAllocation> allocation = geometry.allocate(1000, 1500, 0, "Churn"))
                    allocations.push_back(*allocation);
            }
            else
            {
                const size_t index = random() % allocations.size();
                geometry.free(allocations[index]);
                allocations.erase(allocations.begin() + static_cast<std::ptrdiff_t>(index));
            }
        }
        thread.wait();
    }

    // Small blocks, so the churn needed several and the render thread read ones added meanwhile.
    CHECK(blocksSeen.load() > 1);
    for (const GeometryAllocation& allocation : allocations)
        geometry.free(allocation);
    geometry.collect(device->frameNumber() + rhi::kMaxFramesInFlight);
    CHECK(geometry.stats().allocations == 0);
}

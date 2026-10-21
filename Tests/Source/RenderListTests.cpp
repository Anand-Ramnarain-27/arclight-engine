#include "Core/RangeAllocator.h"
#include "RHI/Backends.h"
#include "RHI/BufferUtils.h"
#include "RHI/Device.h"
#include "Renderer/GeometryPool.h"
#include "Renderer/Model.h"
#include "Renderer/RenderList.h"

#include <doctest/doctest.h>

#include <cstring>
#include <memory>
#include <vector>

using namespace arc;

namespace
{
    // One triangle per primitive, each primitive with its own material, vertices along +X.
    ModelData makeModel(uint32_t primitives, std::vector<MaterialData> materials = {})
    {
        ModelData data;
        data.name = "Test";
        for (uint32_t p = 0; p < primitives; ++p)
        {
            Primitive primitive;
            primitive.firstIndex = static_cast<uint32_t>(data.indices.size());
            primitive.indexCount = 3;
            primitive.baseVertex = static_cast<uint32_t>(data.vertices.size());
            primitive.vertexCount = 3;
            primitive.material = p < materials.size() ? static_cast<int32_t>(p) : -1;
            for (uint32_t v = 0; v < 3; ++v)
            {
                const Vec3 position{static_cast<float>(p) + static_cast<float>(v) * 0.1f, 0.0f, 0.0f};
                data.vertices.push_back({position, {0, 0, 1}, {}});
                primitive.bounds.expand(position);
                data.indices.push_back(v);
            }
            data.primitives.push_back(primitive);
        }
        data.meshes = {{.name = "Mesh", .firstPrimitive = 0, .primitiveCount = primitives}};
        data.materials = std::move(materials);
        return data;
    }
}

TEST_CASE("Range allocator: first fit, and freed neighbours merge")
{
    RangeAllocator ranges(100);
    const auto a = ranges.allocate(30);
    const auto b = ranges.allocate(30);
    const auto c = ranges.allocate(30);
    REQUIRE((a && b && c));
    CHECK(*a == 0);
    CHECK(*b == 30);
    CHECK(*c == 60);
    CHECK(ranges.used() == 90);
    CHECK_FALSE(ranges.allocate(20));

    // Freeing a and c leaves two holes of 30 and 40; neither fits 50.
    ranges.free(*a, 30);
    ranges.free(*c, 30);
    CHECK(ranges.freeRangeCount() == 2);
    CHECK(ranges.largestFreeRange() == 40);
    CHECK_FALSE(ranges.allocate(50));

    // Freeing b joins all three into one range of 100.
    ranges.free(*b, 30);
    CHECK(ranges.freeRangeCount() == 1);
    CHECK(ranges.largestFreeRange() == 100);
    CHECK(ranges.used() == 0);
    CHECK(ranges.allocate(100) == 0u);
}

TEST_CASE("Geometry pool: blocks, oversized models and deferred reuse")
{
    auto device = rhi::createNullDevice({});
    REQUIRE(device);
    GeometryPool pool(*device, {.vertexCapacity = 100, .index16Capacity = 100, .index32Capacity = 100});

    const auto first = pool.allocate(60, 30, 0, "First");
    REQUIRE(first);
    CHECK(first->block == 0);
    CHECK(first->vertices.offset == 0);

    // Does not fit in what is left of block 0, so a second block of the default size is added.
    const auto second = pool.allocate(60, 30, 0, "Second");
    REQUIRE(second);
    CHECK(second->block == 1);

    // Larger than a default block: a block of its own size.
    const auto large = pool.allocate(500, 0, 10, "Large");
    REQUIRE(large);
    CHECK(large->block == 2);
    CHECK(pool.stats().blocks == 3);
    CHECK(pool.stats().vertexBytesCapacity == (100 + 100 + 500) * sizeof(MeshVertex));

    // A freed range stays reserved while frames in flight may read it.
    pool.free(*first);
    CHECK(pool.stats().pendingFrees == 1);
    pool.collect(device->frameNumber() + rhi::kMaxFramesInFlight - 1);
    CHECK(pool.stats().pendingFrees == 1);
    const auto tooEarly = pool.allocate(60, 0, 0, "Too early");
    REQUIRE(tooEarly);
    CHECK(tooEarly->block != 0);

    pool.collect(device->frameNumber() + rhi::kMaxFramesInFlight);
    CHECK(pool.stats().pendingFrees == 0);
    const auto reused = pool.allocate(60, 30, 0, "Reused");
    REQUIRE(reused);
    CHECK(reused->block == 0);
    CHECK(reused->vertices.offset == 0);

    for (const GeometryAllocation& allocation : {*second, *large, *tooEarly, *reused})
        pool.free(allocation);
    CHECK(pool.stats().allocations == 0);
}

TEST_CASE("Models use 16-bit indices where every index fits")
{
    auto device = rhi::createNullDevice({});
    REQUIRE(device);
    GeometryPool pool(*device);

    // Primitive 0 is small; primitive 1 indexes past 65535 and must stay 32-bit.
    ModelData data = makeModel(1);
    Primitive big;
    big.firstIndex = static_cast<uint32_t>(data.indices.size());
    big.baseVertex = static_cast<uint32_t>(data.vertices.size());
    big.vertexCount = 70000;
    big.indexCount = 3;
    data.vertices.resize(data.vertices.size() + big.vertexCount);
    data.indices.insert(data.indices.end(), {0, 1, 69999});
    data.primitives.push_back(big);
    data.meshes[0].primitiveCount = 2;

    const auto model = Model::create(*device, pool, data);
    REQUIRE(model);
    const auto& draws = model->primitiveDraws();
    REQUIRE(draws.size() == 2);
    CHECK(draws[0].indexFormat == rhi::IndexFormat::UInt16);
    CHECK(draws[1].indexFormat == rhi::IndexFormat::UInt32);
    CHECK(draws[1].baseVertex == 3);

    // The 16-bit indices really are in the pool's 16-bit buffer, at the draw's offset.
    const std::vector<std::byte> bytes = rhi::readbackBuffer(*device, pool.indexBuffer(model->geometryBlock(), rhi::IndexFormat::UInt16),
                                                             uint64_t{draws[0].firstIndex} * 2, 6);
    REQUIRE(bytes.size() == 6);
    uint16_t indices[3]{};
    std::memcpy(indices, bytes.data(), sizeof(indices));
    CHECK(indices[0] == 0);
    CHECK(indices[1] == 1);
    CHECK(indices[2] == 2);
}

TEST_CASE("Render list: opaque grouped by state then front to back, blended back to front")
{
    auto device = rhi::createNullDevice({});
    REQUIRE(device);
    GeometryPool pool(*device);

    MaterialData opaque;
    MaterialData doubleSided;
    doubleSided.doubleSided = true;
    MaterialData glass;
    glass.alphaMode = AlphaMode::Blend;
    // Primitives 0 and 1 opaque, 2 double-sided, 3 and 4 blended; primitive p sits at x = p.
    const auto model = Model::create(*device, pool, makeModel(5, {opaque, opaque, doubleSided, glass, glass}));
    REQUIRE(model);

    RenderList list;
    list.addMesh(*model, 0, Mat4::identity());
    CHECK(list.opaque().size() == 3);
    CHECK(list.blended().size() == 2);

    // Camera at x = 10 looking down -X: higher x is nearer.
    list.sort({10, 0, 0}, {-1, 0, 0});
    REQUIRE(list.opaque().size() == 3);
    // Single-sided before double-sided, then within the same material nearer first.
    CHECK(list.opaque()[0].primitive == 0);
    CHECK(list.opaque()[1].primitive == 1);
    CHECK(list.opaque()[2].primitive == 2);
    CHECK(list.blended()[0].primitive == 3);   // Farther (x = 3) drawn first.
    CHECK(list.blended()[1].primitive == 4);

    // Depth only decides between draws with the same state: with one material for both, nearer wins.
    const auto sameMaterial = Model::create(*device, pool, makeModel(2, {opaque}));
    REQUIRE(sameMaterial);
    RenderList depthList;
    depthList.addMesh(*sameMaterial, 0, Mat4::identity());
    depthList.sort({10, 0, 0}, {-1, 0, 0});
    REQUIRE(depthList.opaque().size() == 2);
    CHECK(depthList.opaque()[0].viewDepth < depthList.opaque()[1].viewDepth);

    // Mirrored transforms need the other winding, so they sort into their own group.
    RenderList mirrored;
    mirrored.addMesh(*model, 0, Mat4::scaling({-1, 1, 1}));
    mirrored.addMesh(*model, 0, Mat4::identity());
    mirrored.sort({10, 0, 0}, {-1, 0, 0});
    CHECK(mirrored.transforms()[mirrored.opaque()[0].transform].mirrored == false);
    CHECK(mirrored.transforms()[mirrored.opaque().back().transform].mirrored == true);

    // Unsorted keeps the order of addition.
    RenderList unsorted;
    unsorted.addMesh(*model, 0, Mat4::scaling({-1, 1, 1}));
    unsorted.addMesh(*model, 0, Mat4::identity());
    unsorted.sort({10, 0, 0}, {-1, 0, 0}, false);
    CHECK(unsorted.opaque()[0].transform == 0);
}

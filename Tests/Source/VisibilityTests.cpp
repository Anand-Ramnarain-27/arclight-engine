#include "Math/Frustum.h"
#include "RHI/Backends.h"
#include "RHI/Device.h"
#include "RHI/Texture.h"
#include "RHI/TextureUtils.h"
#include "Renderer/CubeMap.h"
#include "Renderer/LodGenerator.h"
#include "Renderer/RenderList.h"

#include <doctest/doctest.h>

#include <cmath>
#include <limits>
#include <span>
#include <vector>

using namespace arc;

namespace
{
    // Looking down -Z from the origin with a 90 degree field of view, so at depth d the view spans
    // x and y in [-d, d].
    Frustum testFrustum(float farZ)
    {
        return Frustum::fromCamera({0, 0, 0}, {0, 0, -1}, {1, 0, 0}, {0, 1, 0}, kHalfPi, 1.0f, 0.1f, farZ);
    }

    Aabb boxAt(const Vec3& centre, float halfSize)
    {
        return {centre - Vec3{halfSize}, centre + Vec3{halfSize}};
    }

    // A square grid of n x n quads with a wavy height, so simplifying it moves the surface.
    ModelData wavyGrid(uint32_t n)
    {
        ModelData data;
        for (uint32_t z = 0; z <= n; ++z)
            for (uint32_t x = 0; x <= n; ++x)
            {
                const float fx = static_cast<float>(x) / static_cast<float>(n);
                const float fz = static_cast<float>(z) / static_cast<float>(n);
                data.vertices.push_back({{fx, 0.05f * std::sin(fx * 12.0f) * std::cos(fz * 9.0f), fz}, {0, 1, 0}, {fx, fz}});
            }
        for (uint32_t z = 0; z < n; ++z)
            for (uint32_t x = 0; x < n; ++x)
            {
                const uint32_t i = z * (n + 1) + x;
                data.indices.insert(data.indices.end(), {i, i + n + 1, i + 1, i + 1, i + n + 1, i + n + 2});
            }
        Primitive primitive;
        primitive.indexCount = static_cast<uint32_t>(data.indices.size());
        primitive.vertexCount = static_cast<uint32_t>(data.vertices.size());
        for (const MeshVertex& vertex : data.vertices)
            primitive.bounds.expand(vertex.position);
        data.primitives = {primitive};
        data.meshes = {{.name = "Grid", .firstPrimitive = 0, .primitiveCount = 1}};
        return data;
    }
}

TEST_CASE("Frustum: boxes inside, outside and across its planes")
{
    const Frustum frustum = testFrustum(100.0f);
    CHECK(frustum.planeCount == 6);
    CHECK(frustum.classify(boxAt({0, 0, -10}, 1)) == Containment::Inside);
    CHECK(frustum.classify(boxAt({0, 0, 10}, 1)) == Containment::Outside);       // Behind.
    CHECK(frustum.classify(boxAt({20, 0, -10}, 1)) == Containment::Outside);     // Right of the view.
    CHECK(frustum.classify(boxAt({0, -20, -10}, 1)) == Containment::Outside);    // Below.
    CHECK(frustum.classify(boxAt({10, 0, -10}, 1)) == Containment::Intersects);  // Across the right plane.
    CHECK(frustum.classify(boxAt({0, 0, -100}, 1)) == Containment::Intersects);  // Across the far plane.
    CHECK(frustum.classify(boxAt({0, 0, -1000}, 1)) == Containment::Outside);
    CHECK(frustum.classify(Aabb{}) == Containment::Outside);

    // Without a far plane, as with reversed-Z, distance alone never culls.
    const Frustum infinite = testFrustum(std::numeric_limits<float>::infinity());
    CHECK(infinite.planeCount == 5);
    CHECK(infinite.classify(boxAt({0, 0, -1e6f}, 1)) == Containment::Inside);
}

TEST_CASE("Frustum: oriented boxes cull rotated meshes their world box would keep")
{
    const Frustum frustum = testFrustum(100.0f);

    // A thin stick along (1, 0, 1), entirely left of the left plane (x = z for x < 0). Its
    // world-aligned box reaches into the view, but the stick itself never does.
    const Aabb stick{{-10, -0.05f, -0.05f}, {10, 0.05f, 0.05f}};
    const Mat4 world = Mat4::translation({-12, 0, -5}) * Mat4::rotation(Quat::fromAxisAngle(Vec3::up(), -kPi * 0.25f));
    CHECK(frustum.classify(stick.transformed(world)) == Containment::Intersects);
    CHECK(frustum.classify(Obb::fromAabb(stick, world)) == Containment::Outside);

    // Without rotation the two agree.
    for (const Vec3& centre : {Vec3{0, 0, -10}, Vec3{10, 0, -10}, Vec3{20, 0, -10}})
        CHECK(frustum.classify(Obb::fromAabb(boxAt({0, 0, 0}, 1), Mat4::translation(centre))) ==
              frustum.classify(boxAt(centre, 1)));
}

TEST_CASE("Cube faces: directions and face points are inverses, in the D3D12 and Metal layout")
{
    for (uint32_t face = 0; face < 6; ++face)
        for (const float s : {-0.9f, -0.3f, 0.0f, 0.5f, 0.9f})
            for (const float t : {-0.7f, 0.0f, 0.8f})
            {
                const CubeFacePoint point = cubeFacePoint(cubeFaceDirection(static_cast<CubeFace>(face), s, t));
                CHECK(point.face == static_cast<CubeFace>(face));
                CHECK(nearlyEqual(point.s, s));
                CHECK(nearlyEqual(point.t, t));
            }

    // The top edge of the +Y face borders the -Z face, and the +Z face's right edge the +X face.
    CHECK(nearlyEqual(normalize(cubeFaceDirection(CubeFace::PositiveY, 0, -1)), normalize(Vec3{0, 1, -1})));
    CHECK(nearlyEqual(normalize(cubeFaceDirection(CubeFace::PositiveZ, 1, 0)), normalize(Vec3{1, 0, 1})));

    // Looking forward in the world (-Z) samples the +Z face.
    CHECK(cubeFacePoint(worldToCube({0, 0, -1})).face == CubeFace::PositiveZ);
}

TEST_CASE("Cube textures upload and read back face by face")
{
    auto device = rhi::createNullDevice({});
    REQUIRE(device);
    const rhi::TextureDesc desc{.size = {4, 4},
                                .format = rhi::Format::RGBA8_UNORM,
                                .mipLevels = 2,
                                .usage = rhi::TextureUsage::Sampled,
                                .type = rhi::TextureType::Cube,
                                .debugName = "Test Cube"};
    CHECK(rhi::arrayLayerCount(desc) == 6);
    CHECK(rhi::computeTextureCopyLayout(desc).levels.size() == 12);

    auto texture = device->createTexture(desc);
    REQUIRE(texture);
    std::vector<std::vector<std::byte>> data;
    std::vector<std::span<const std::byte>> spans;
    for (uint32_t layer = 0; layer < 6; ++layer)
        for (uint32_t mip = 0; mip < 2; ++mip)
        {
            const uint32_t side = mip == 0 ? 4 : 2;
            data.emplace_back(side * side * 4, static_cast<std::byte>(layer * 16 + mip));
        }
    for (const auto& level : data)
        spans.emplace_back(level);
    REQUIRE(rhi::uploadTexture(*device, *texture, spans));

    for (uint32_t layer = 0; layer < 6; ++layer)
        for (uint32_t mip = 0; mip < 2; ++mip)
            CHECK(rhi::readbackTexture(*device, *texture, mip, layer) == data[rhi::subresourceIndex(desc, mip, layer)]);
}

TEST_CASE("Generated levels of detail have fewer triangles and growing error")
{
    ModelData data = wavyGrid(40);
    const size_t fullIndices = data.indices.size();
    generateLods(data);

    const Primitive& primitive = data.primitives[0];
    REQUIRE(primitive.lodCount >= 2);
    uint32_t previousCount = primitive.indexCount;
    float previousError = 0.0f;
    for (uint32_t level = 0; level < primitive.lodCount; ++level)
    {
        const LodLevel& lod = primitive.lods[level];
        CHECK(lod.indexCount % 3 == 0);
        CHECK(lod.indexCount < previousCount);
        CHECK(lod.error >= previousError);
        CHECK(lod.firstIndex >= fullIndices);
        for (uint32_t i = 0; i < lod.indexCount; ++i)
            CHECK(data.indices[lod.firstIndex + i] < primitive.vertexCount);
        previousCount = lod.indexCount;
        previousError = lod.error;
    }

    // Too small to be worth simplifying.
    ModelData small = wavyGrid(4);
    generateLods(small);
    CHECK(small.primitives[0].lodCount == 0);
}

TEST_CASE("LOD selection keeps the projected error under the pixel limit")
{
    const float errors[] = {0.0f, 0.01f, 0.1f};
    const Aabb bounds{{-0.5f, -0.5f, -0.5f}, {0.5f, 0.5f, 0.5f}};
    const float radius = length(bounds.size()) * 0.5f;
    LodPolicy policy{.cameraPosition = {0, 0, 0}, .pixelsPerUnit = 1000.0f, .maxErrorPixels = 1.0f};

    // Level 1's error covers one pixel at 10 units, level 2's at 100.
    const auto at = [&](float distance, const Mat4& scale = Mat4::identity()) {
        return policy.select(errors, bounds, Mat4::translation({0, 0, -(distance + radius)}) * scale);
    };
    CHECK(at(5.0f) == 0);
    CHECK(at(50.0f) == 1);
    CHECK(at(500.0f) == 2);
    // Scaled up 2x, the same error is twice as large in the world.
    CHECK(at(15.0f, Mat4::scaling(Vec3{2})) == 0);
    CHECK(at(0.0f) == 0);   // Inside the bounds.

    policy.forcedLevel = 5;
    CHECK(at(5.0f) == 2);
    policy.forcedLevel = 1;
    CHECK(at(500.0f) == 1);
}

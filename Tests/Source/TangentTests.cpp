#include "Renderer/TangentGenerator.h"

#include <doctest/doctest.h>

#include <vector>

using namespace arc;

namespace
{
    bool near(const Vec3& a, const Vec3& b) { return nearlyEqual(a, b, 1e-4f); }

    Vec3 xyz(const Vec4& v) { return {v.x, v.y, v.z}; }

    MeshVertex vertex(Vec3 position, Vec2 uv)
    {
        MeshVertex v;
        v.position = position;
        v.normal = {0.0f, 0.0f, 1.0f};
        v.uv = uv;
        return v;
    }

    // A unit quad facing +Z, its texture upright: glTF's v grows down the image, so the top edge
    // (y = 1) has v = 0.
    std::vector<MeshVertex> uprightQuad(float x0 = 0.0f, bool mirrorU = false)
    {
        const auto u = [mirrorU](float value) { return mirrorU ? 1.0f - value : value; };
        return {vertex({x0, 0, 0}, {u(0), 1}), vertex({x0 + 1, 0, 0}, {u(1), 1}), vertex({x0, 1, 0}, {u(0), 0}),
                vertex({x0 + 1, 1, 0}, {u(1), 0})};
    }

    // The bitangent glTF derives from a vertex.
    Vec3 bitangent(const MeshVertex& v) { return cross(v.normal, xyz(v.tangent)) * v.tangent.w; }
}

TEST_CASE("MikkTSpace tangents follow u, with the bitangent up the image")
{
    std::vector<MeshVertex> vertices = uprightQuad();
    std::vector<uint32_t> indices = {0, 1, 2, 2, 1, 3};
    generateTangents(vertices, indices);

    REQUIRE(vertices.size() == 4);
    REQUIRE(indices.size() == 6);
    for (const MeshVertex& v : vertices)
    {
        CHECK(near(xyz(v.tangent), {1, 0, 0}));
        CHECK(v.tangent.w == 1.0f);
        // Up the image is +Y, where a normal map's green channel points.
        CHECK(near(bitangent(v), {0, 1, 0}));
    }
}

TEST_CASE("A texture mirrored in u flips the tangent and the handedness, not the bitangent")
{
    std::vector<MeshVertex> vertices = uprightQuad(0.0f, true);
    std::vector<uint32_t> indices = {0, 1, 2, 2, 1, 3};
    generateTangents(vertices, indices);
    for (const MeshVertex& v : vertices)
    {
        CHECK(near(xyz(v.tangent), {-1, 0, 0}));
        CHECK(v.tangent.w == -1.0f);
        CHECK(near(bitangent(v), {0, 1, 0}));
    }
}

TEST_CASE("Vertices on a mirror seam are split, and every triangle keeps its corners")
{
    // Two quads side by side, sharing the edge x = 1, the right one mirrored: the shared vertices
    // need a different tangent on each side.
    std::vector<MeshVertex> left = uprightQuad(0.0f);
    std::vector<MeshVertex> right = uprightQuad(1.0f, true);
    std::vector<MeshVertex> vertices = {left[0], left[1], left[2], left[3], right[1], right[3]};
    // right[0] and right[2] are at x = 1 like left[1] and left[3], with the same u (1), so the input
    // shares them.
    std::vector<uint32_t> indices = {0, 1, 2, 2, 1, 3, 1, 4, 3, 3, 4, 5};
    const std::vector<MeshVertex> original = vertices;
    const std::vector<uint32_t> originalIndices = indices;

    generateTangents(vertices, indices);
    CHECK(vertices.size() == 8);
    REQUIRE(indices.size() == originalIndices.size());
    for (size_t i = 0; i < indices.size(); ++i)
    {
        CHECK(near(vertices[indices[i]].position, original[originalIndices[i]].position));
        const float expectedX = i < 6 ? 1.0f : -1.0f;
        CHECK(vertices[indices[i]].tangent.x == doctest::Approx(expectedX).epsilon(1e-4));
    }
}

TEST_CASE("Default tangents are unit length and perpendicular to the normal")
{
    std::vector<MeshVertex> vertices(3);
    vertices[0].normal = {0, 1, 0};
    vertices[1].normal = normalize(Vec3{1, 1, 1});
    vertices[2].normal = {0, 0, -1};
    fillDefaultTangents(vertices);
    for (const MeshVertex& v : vertices)
    {
        CHECK(length(xyz(v.tangent)) == doctest::Approx(1.0f));
        CHECK(dot(xyz(v.tangent), v.normal) == doctest::Approx(0.0f).epsilon(1e-5));
        CHECK(v.tangent.w == 1.0f);
    }
}

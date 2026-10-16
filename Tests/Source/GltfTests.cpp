#include "RHI/Backends.h"
#include "RHI/Device.h"
#include "RHI/Texture.h"
#include "Renderer/GltfImporter.h"
#include "Renderer/Model.h"

#include <doctest/doctest.h>

#include <cstdint>
#include <cstring>
#include <span>
#include <string>
#include <vector>

using namespace arc;

namespace
{
    // Builds the binary buffer of a test file, one typed value at a time.
    struct Bytes
    {
        std::vector<uint8_t> data;

        template <typename T>
        Bytes& add(std::initializer_list<T> values)
        {
            for (const T value : values)
            {
                const size_t offset = data.size();
                data.resize(offset + sizeof(T));
                std::memcpy(data.data() + offset, &value, sizeof(T));
            }
            return *this;
        }

        Bytes& padTo4()
        {
            data.resize((data.size() + 3) / 4 * 4, 0);
            return *this;
        }
    };

    std::string base64(const std::vector<uint8_t>& bytes)
    {
        static constexpr char kAlphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
        std::string out;
        for (size_t i = 0; i < bytes.size(); i += 3)
        {
            const uint32_t chunk = (uint32_t{bytes[i]} << 16) | (i + 1 < bytes.size() ? uint32_t{bytes[i + 1]} << 8 : 0) |
                                   (i + 2 < bytes.size() ? uint32_t{bytes[i + 2]} : 0);
            out += kAlphabet[(chunk >> 18) & 63];
            out += kAlphabet[(chunk >> 12) & 63];
            out += i + 1 < bytes.size() ? kAlphabet[(chunk >> 6) & 63] : '=';
            out += i + 2 < bytes.size() ? kAlphabet[chunk & 63] : '=';
        }
        return out;
    }

    // A .gltf file whose single buffer is embedded as a data URI. `body` is the rest of the JSON
    // object, without the braces, "asset" or "buffers".
    std::optional<ModelData> importJson(const std::string& body, const Bytes& buffer)
    {
        const std::string json = R"({"asset": {"version": "2.0"}, "buffers": [{"byteLength": )" +
                                 std::to_string(buffer.data.size()) + R"(, "uri": "data:application/octet-stream;base64,)" +
                                 base64(buffer.data) + R"("}], )" + body + "}";
        return importGltf(std::as_bytes(std::span(json)), "", "test.gltf");
    }

    bool near(const Vec3& a, const Vec3& b) { return nearlyEqual(a, b, 1e-5f); }

    // A 1x1 opaque red PNG.
    constexpr const char* kRedPixelPng =
        "iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAYAAAAfFcSJAAAADUlEQVR42mP8z8DwHwAFBQIAX8jx0gAAAABJRU5ErkJggg==";
}

TEST_CASE("glTF: interleaved attributes, 8-bit indices and a node hierarchy")
{
    // Three vertices of position + normal, interleaved with a 24-byte stride, then three indices.
    Bytes buffer;
    buffer.add<float>({0, 0, 0, 0, 0, 1,   1, 0, 0, 0, 0, 1,   0, 1, 0, 0, 0, 1});
    buffer.add<uint8_t>({0, 1, 2}).padTo4();

    const auto model = importJson(R"(
        "bufferViews": [
            {"buffer": 0, "byteOffset": 0, "byteLength": 72, "byteStride": 24},
            {"buffer": 0, "byteOffset": 72, "byteLength": 3}
        ],
        "accessors": [
            {"bufferView": 0, "byteOffset": 0, "componentType": 5126, "count": 3, "type": "VEC3"},
            {"bufferView": 0, "byteOffset": 12, "componentType": 5126, "count": 3, "type": "VEC3"},
            {"bufferView": 1, "componentType": 5121, "count": 3, "type": "SCALAR"}
        ],
        "meshes": [{"name": "Triangle", "primitives": [{"attributes": {"POSITION": 0, "NORMAL": 1}, "indices": 2}]}],
        "nodes": [
            {"name": "Parent", "translation": [1, 2, 3], "children": [1]},
            {"name": "Child", "scale": [2, 2, 2], "mesh": 0}
        ],
        "scenes": [{"nodes": [0]}], "scene": 0)",
                                  buffer);
    REQUIRE(model);

    REQUIRE(model->vertices.size() == 3);
    CHECK(near(model->vertices[1].position, {1, 0, 0}));
    CHECK(near(model->vertices[2].normal, {0, 0, 1}));
    CHECK(model->indices == std::vector<uint32_t>{0, 1, 2});

    REQUIRE(model->primitives.size() == 1);
    CHECK(model->primitives[0].indexCount == 3);
    CHECK(model->primitives[0].material == -1);

    // The child's transform includes its parent's: translate, then (applied first) scale.
    REQUIRE(model->instances.size() == 1);
    CHECK(model->instances[0].name == "Child");
    CHECK(near(model->instances[0].world.transformPoint({1, 0, 0}), {3, 2, 3}));
    CHECK(near(model->bounds.min, {1, 2, 3}));
    CHECK(near(model->bounds.max, {3, 4, 3}));
}

TEST_CASE("glTF: primitives without normals get flat normals")
{
    // A square in the XY plane, two triangles sharing an edge, no NORMAL attribute.
    Bytes buffer;
    buffer.add<float>({0, 0, 0,   1, 0, 0,   1, 1, 0,   0, 1, 0});
    buffer.add<uint16_t>({0, 1, 2, 0, 2, 3});

    const auto model = importJson(R"(
        "bufferViews": [{"buffer": 0, "byteOffset": 0, "byteLength": 48}, {"buffer": 0, "byteOffset": 48, "byteLength": 12}],
        "accessors": [
            {"bufferView": 0, "componentType": 5126, "count": 4, "type": "VEC3"},
            {"bufferView": 1, "componentType": 5123, "count": 6, "type": "SCALAR"}
        ],
        "meshes": [{"primitives": [{"attributes": {"POSITION": 0}, "indices": 1}]}],
        "nodes": [{"mesh": 0}])",
                                  buffer);
    REQUIRE(model);

    // Shared vertices are split so every triangle has its own: 6 vertices instead of 4.
    CHECK(model->vertices.size() == 6);
    for (const MeshVertex& vertex : model->vertices)
        CHECK(near(vertex.normal, {0, 0, 1}));
    CHECK(model->instances.size() == 1);   // No scene: the root nodes are used.
}

TEST_CASE("glTF: normalized integer texture coordinates")
{
    Bytes buffer;
    buffer.add<float>({0, 0, 0,   1, 0, 0,   0, 1, 0});
    buffer.add<uint16_t>({0, 0,   65535, 0,   32768, 65535});

    const auto model = importJson(R"(
        "bufferViews": [{"buffer": 0, "byteOffset": 0, "byteLength": 36}, {"buffer": 0, "byteOffset": 36, "byteLength": 12}],
        "accessors": [
            {"bufferView": 0, "componentType": 5126, "count": 3, "type": "VEC3"},
            {"bufferView": 1, "componentType": 5123, "normalized": true, "count": 3, "type": "VEC2"}
        ],
        "meshes": [{"primitives": [{"attributes": {"POSITION": 0, "TEXCOORD_0": 1}}]}],
        "nodes": [{"mesh": 0}])",
                                  buffer);
    REQUIRE(model);
    REQUIRE(model->vertices.size() == 3);
    CHECK(model->vertices[1].uv.x == doctest::Approx(1.0f));
    CHECK(model->vertices[2].uv.x == doctest::Approx(32768.0f / 65535.0f));
    CHECK(model->vertices[2].uv.y == doctest::Approx(1.0f));
}

TEST_CASE("glTF: triangle strips become lists with the same winding")
{
    Bytes buffer;
    buffer.add<float>({0, 0, 0,   0, 1, 0,   1, 0, 0,   1, 1, 0});

    const auto model = importJson(R"(
        "bufferViews": [{"buffer": 0, "byteLength": 48}],
        "accessors": [{"bufferView": 0, "componentType": 5126, "count": 4, "type": "VEC3"}],
        "meshes": [{"primitives": [{"attributes": {"POSITION": 0}, "mode": 5}]}],
        "nodes": [{"mesh": 0}])",
                                  buffer);
    REQUIRE(model);
    // The strip (0 1 2 3) is triangles (0 1 2) and (2 1 3); with flat normals they are split into
    // six vertices, and both triangles face the same way.
    REQUIRE(model->vertices.size() == 6);
    CHECK(near(model->vertices[0].normal, model->vertices[3].normal));
    CHECK(near(model->vertices[3].position, {1, 0, 0}));
    CHECK(near(model->vertices[4].position, {0, 1, 0}));
}

TEST_CASE("glTF: sparse accessors replace some elements of their base")
{
    // Base positions, then one sparse index (2) and its replacement value.
    Bytes buffer;
    buffer.add<float>({0, 0, 0,   1, 0, 0,   0, 1, 0});
    buffer.add<uint16_t>({2, 0});
    buffer.add<float>({0, 5, 0});

    const auto model = importJson(R"(
        "bufferViews": [
            {"buffer": 0, "byteOffset": 0, "byteLength": 36},
            {"buffer": 0, "byteOffset": 36, "byteLength": 2},
            {"buffer": 0, "byteOffset": 40, "byteLength": 12}
        ],
        "accessors": [{"bufferView": 0, "componentType": 5126, "count": 3, "type": "VEC3",
            "sparse": {"count": 1, "indices": {"bufferView": 1, "componentType": 5123}, "values": {"bufferView": 2}}}],
        "meshes": [{"primitives": [{"attributes": {"POSITION": 0}}]}],
        "nodes": [{"mesh": 0}])",
                                  buffer);
    REQUIRE(model);
    REQUIRE(model->vertices.size() == 3);
    CHECK(near(model->vertices[1].position, {1, 0, 0}));
    CHECK(near(model->vertices[2].position, {0, 5, 0}));
}

TEST_CASE("glTF: broken accessors are skipped instead of read past the buffer")
{
    Bytes buffer;
    buffer.add<float>({0, 0, 0,   1, 0, 0,   0, 1, 0});
    buffer.add<uint16_t>({0, 1, 7, 0});   // Index 7 does not exist.

    const auto model = importJson(R"(
        "bufferViews": [{"buffer": 0, "byteOffset": 0, "byteLength": 36}, {"buffer": 0, "byteOffset": 36, "byteLength": 8}],
        "accessors": [
            {"bufferView": 0, "componentType": 5126, "count": 3, "type": "VEC3"},
            {"bufferView": 0, "componentType": 5126, "count": 1000, "type": "VEC3"},
            {"bufferView": 1, "componentType": 5123, "count": 3, "type": "SCALAR"}
        ],
        "meshes": [{"primitives": [
            {"attributes": {"POSITION": 1}},
            {"attributes": {"POSITION": 0}, "indices": 2},
            {"attributes": {"POSITION": 0}}
        ]}],
        "nodes": [{"mesh": 0}])",
                                  buffer);
    REQUIRE(model);
    // Only the third primitive survives: the first reads 1000 vertices from a 36-byte view, the
    // second indexes a vertex that does not exist.
    REQUIRE(model->primitives.size() == 1);
    CHECK(model->primitives[0].vertexCount == 3);
    CHECK(model->meshes[0].primitiveCount == 1);
}

TEST_CASE("glTF: materials, samplers and embedded images")
{
    Bytes buffer;
    buffer.add<float>({0, 0, 0,   1, 0, 0,   0, 1, 0});

    const auto model = importJson(std::string(R"(
        "bufferViews": [{"buffer": 0, "byteLength": 36}],
        "accessors": [{"bufferView": 0, "componentType": 5126, "count": 3, "type": "VEC3"}],
        "images": [{"uri": "data:image/png;base64,)") + kRedPixelPng + R"("}, {"uri": "data:image/png;base64,AAAA"}],
        "samplers": [{"magFilter": 9728, "minFilter": 9728, "wrapS": 33071, "wrapT": 33648}],
        "textures": [{"source": 0, "sampler": 0}, {"source": 1}],
        "materials": [
            {"name": "Leaf", "pbrMetallicRoughness": {"baseColorFactor": [0.5, 1, 0.25, 1], "baseColorTexture": {"index": 0},
             "metallicFactor": 0, "roughnessFactor": 0.7}, "alphaMode": "MASK", "alphaCutoff": 0.3, "doubleSided": true},
            {"name": "Broken", "pbrMetallicRoughness": {"baseColorTexture": {"index": 1}}, "alphaMode": "BLEND"}
        ],
        "meshes": [{"primitives": [{"attributes": {"POSITION": 0}, "material": 0}, {"attributes": {"POSITION": 0}, "material": 1}]}],
        "nodes": [{"mesh": 0}])",
                                  buffer);
    REQUIRE(model);

    REQUIRE(model->materials.size() == 2);
    const MaterialData& leaf = model->materials[0];
    CHECK(leaf.name == "Leaf");
    CHECK(leaf.baseColorFactor.y == 1.0f);
    CHECK(leaf.baseColorFactor.z == 0.25f);
    CHECK(leaf.roughnessFactor == doctest::Approx(0.7f));
    CHECK(leaf.alphaMode == AlphaMode::Mask);
    CHECK(leaf.alphaCutoff == doctest::Approx(0.3f));
    CHECK(leaf.doubleSided);
    CHECK(model->materials[1].alphaMode == AlphaMode::Blend);

    REQUIRE(model->textures.size() == 2);
    const rhi::SamplerDesc& sampler = model->textures[0].sampler;
    CHECK(sampler.magFilter == rhi::Filter::Nearest);
    CHECK(sampler.minFilter == rhi::Filter::Nearest);
    CHECK(sampler.maxLod == 0.0f);   // A minification filter without a mipmap mode turns mipmapping off.
    CHECK(sampler.addressU == rhi::AddressMode::ClampToEdge);
    CHECK(sampler.addressV == rhi::AddressMode::MirroredRepeat);
    CHECK(model->textures[1].sampler.maxAnisotropy > 1);   // No sampler: the importer's default.

    REQUIRE(model->images.size() == 2);
    REQUIRE(model->images[0].size.width == 1);
    CHECK(model->images[0].pixels[0] == std::byte{255});
    CHECK(model->images[1].empty());   // Not a decodable image.

    // On the GPU, the undecodable texture is left null for the renderer's "missing" stand-in.
    auto device = rhi::createNullDevice({});
    REQUIRE(device);
    const auto gpuModel = Model::create(*device, *model);
    REQUIRE(gpuModel);
    CHECK(gpuModel->texture(0) != nullptr);
    CHECK(gpuModel->texture(0)->desc().format == rhi::Format::RGBA8_UNORM_SRGB);
    CHECK(gpuModel->sampler(0) != nullptr);
    CHECK(gpuModel->texture(1) == nullptr);
    CHECK(gpuModel->textureCount() == 1);
}

TEST_CASE("glTF: binary files")
{
    Bytes bin;
    bin.add<float>({0, 0, 0,   1, 0, 0,   0, 1, 0});
    std::string json = R"({"asset": {"version": "2.0"}, "buffers": [{"byteLength": 36}],
        "bufferViews": [{"buffer": 0, "byteLength": 36}],
        "accessors": [{"bufferView": 0, "componentType": 5126, "count": 3, "type": "VEC3"}],
        "meshes": [{"primitives": [{"attributes": {"POSITION": 0}}]}], "nodes": [{"mesh": 0}]})";
    json.resize((json.size() + 3) / 4 * 4, ' ');   // Chunks are 4-byte aligned; JSON pads with spaces.

    // Header, JSON chunk, BIN chunk: each chunk is its length, its type, then its data.
    Bytes glb;
    const auto total = static_cast<uint32_t>(12 + 8 + json.size() + 8 + bin.data.size());
    glb.add<uint32_t>({0x46546C67, 2, total});   // "glTF", version 2.
    glb.add<uint32_t>({static_cast<uint32_t>(json.size()), 0x4E4F534A});   // "JSON"
    glb.data.insert(glb.data.end(), json.begin(), json.end());
    glb.add<uint32_t>({static_cast<uint32_t>(bin.data.size()), 0x004E4942});   // "BIN\0"
    glb.data.insert(glb.data.end(), bin.data.begin(), bin.data.end());

    const auto model = importGltf(std::as_bytes(std::span(glb.data)), "", "test.glb");
    REQUIRE(model);
    REQUIRE(model->vertices.size() == 3);
    CHECK(near(model->vertices[2].position, {0, 1, 0}));
}

TEST_CASE("glTF: unsupported required extensions and unreadable files fail")
{
    Bytes buffer;
    buffer.add<float>({0, 0, 0});
    CHECK_FALSE(importJson(R"("extensionsRequired": ["KHR_draco_mesh_compression"],
        "extensionsUsed": ["KHR_draco_mesh_compression"])",
                           buffer));
    CHECK_FALSE(importGltf("Does/Not/Exist.gltf"));

    const std::string garbage = "not a glTF file";
    CHECK_FALSE(importGltf(std::as_bytes(std::span(garbage)), "", "garbage.gltf"));
}

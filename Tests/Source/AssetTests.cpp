#include "Assets/AssetDatabase.h"
#include "Assets/AssetImporters.h"
#include "Assets/AssetMeta.h"
#include "Assets/CookedFormats.h"
#include "Assets/ResourceManager.h"
#include "Core/BinaryStream.h"
#include "Core/Hash.h"
#include "Platform/VirtualFileSystem.h"
#include "RHI/Backends.h"
#include "RHI/Device.h"
#include "RHI/Texture.h"
#include "Renderer/GeometryPool.h"
#include "Renderer/LodGenerator.h"
#include "Renderer/Model.h"

#include "TestAssets.h"

#include <doctest/doctest.h>

#include <cmath>
#include <memory>
#include <string>
#include <vector>

using namespace arc;
using arc::test::makeBmp;
using arc::test::triangleGltf;

namespace
{
    std::span<const std::byte> bytesOf(std::string_view text)
    {
        return std::as_bytes(std::span(text.data(), text.size()));
    }

    ModelData sampleModel()
    {
        ModelData data;
        data.name = "Sample";
        data.vertices = {{{0, 0, 0}, {0, 0, 1}, {0, 0}}, {{1, 0, 0}, {0, 0, 1}, {1, 0}}, {{0, 1, 0}, {0, 0, 1}, {0, 1}},
                         {{1, 1, 0}, {0, 0, 1}, {1, 1}}};
        data.indices = {0, 1, 2, 2, 1, 3, 0, 1, 2};
        Primitive primitive;
        primitive.indexCount = 6;
        primitive.vertexCount = 4;
        primitive.material = 0;
        primitive.bounds.expand(Vec3{0, 0, 0});
        primitive.bounds.expand(Vec3{1, 1, 0});
        primitive.lods[0] = {.firstIndex = 6, .indexCount = 3, .error = 0.25f};
        primitive.lodCount = 1;
        data.primitives = {primitive};
        data.meshes = {{.name = "Quad", .firstPrimitive = 0, .primitiveCount = 1}};
        data.instances = {{.name = "Quad", .world = Mat4::translation({2, 0, 0}), .mesh = 0}};
        data.nodes.resize(1);
        data.nodes[0].name = "Quad";
        data.nodes[0].translation = {2, 0, 0};
        data.nodes[0].mesh = 0;
        MaterialData material;
        material.name = "Red";
        material.baseColorFactor = {1, 0, 0, 0.5f};
        material.baseColorTexture = 0;
        material.metallicRoughnessTexture = 1;
        material.metallicFactor = 0.25f;
        material.normalTexture = 1;
        material.normalScale = 0.5f;
        material.occlusionTexture = 1;
        material.occlusionStrength = 0.75f;
        material.emissiveTexture = 0;
        material.emissiveFactor = {2, 1, 0};
        material.alphaMode = AlphaMode::Blend;
        material.doubleSided = true;
        data.materials = {material};
        TextureData texture;
        texture.image = 0;
        texture.sampler.addressU = rhi::AddressMode::ClampToEdge;
        texture.sampler.maxAnisotropy = 8;
        TextureData dataTexture;
        dataTexture.image = 1;
        data.textures = {texture, dataTexture};
        Image image;
        image.size = {4, 2};
        image.pixels.assign(4 * 2 * 4, std::byte{200});
        data.imageMips = {generateMipChain(image, ColorSpace::Srgb), generateMipChain(image, ColorSpace::Linear)};
        data.imageColorSpaces = {ColorSpace::Srgb, ColorSpace::Linear};
        data.images.resize(2);
        data.bounds.expand(Vec3{2, 0, 0});
        data.bounds.expand(Vec3{3, 1, 0});
        return data;
    }

    const CookedHeader kHeader{{.id = {42}, .importerVersion = 7, .sourceHash = 0x1234, .settingsHash = 0x5678}, {99, 1000}};

    struct Project
    {
        Project()
        {
            fileSystem.mount("assets", assets);
            fileSystem.mount("library", library);
        }

        VirtualFileSystem fileSystem;
        std::shared_ptr<MemorySource> assets = std::make_shared<MemorySource>(true);
        std::shared_ptr<MemorySource> library = std::make_shared<MemorySource>(true);
    };
}

TEST_CASE("FNV-1a matches its published test values")
{
    CHECK(fnv1a64(std::string_view()) == kFnv1aBasis);
    CHECK(fnv1a64(std::string_view("a")) == 0xaf63dc4c8601ec8cull);
    CHECK(fnv1a64(std::string_view("foobar")) == 0x85944171f73967e8ull);
    CHECK(fnv1a64(bytesOf("foobar")) == fnv1a64(std::string_view("foobar")));
    CHECK(fnv1a64(std::string_view("bar"), fnv1a64(std::string_view("foo"))) == fnv1a64(std::string_view("foobar")));
}

TEST_CASE("Binary streams round-trip and refuse to read past the end")
{
    BinaryWriter writer;
    writer.write(uint32_t{7});
    writer.write(-2.5f);
    writer.writeString("hello");
    const std::vector<uint16_t> values{1, 2, 3};
    writer.writeArray(std::span<const uint16_t>(values));
    const std::vector<std::byte> bytes = writer.take();

    BinaryReader reader(bytes);
    uint32_t number = 0;
    float real = 0.0f;
    std::string text;
    std::vector<uint16_t> array;
    CHECK(reader.read(number));
    CHECK(reader.read(real));
    CHECK(reader.readString(text));
    CHECK(reader.readArray(array, 10));
    CHECK(number == 7);
    CHECK(real == -2.5f);
    CHECK(text == "hello");
    CHECK(array == values);
    CHECK(reader.atEnd());
    CHECK_FALSE(reader.read(number));
    CHECK(reader.failed());

    SUBCASE("a count above the limit fails")
    {
        BinaryReader limited(bytes);
        limited.read(number);
        limited.read(real);
        limited.readString(text);
        CHECK_FALSE(limited.readArray(array, 2));
        CHECK(limited.failed());
    }

    SUBCASE("a string longer than the data fails, and so does everything after it")
    {
        BinaryWriter liar;
        liar.write(uint32_t{1000});
        liar.writeString("x");
        const std::vector<std::byte> lie = liar.take();
        BinaryReader truncated(lie);
        CHECK_FALSE(truncated.readString(text));
        uint32_t later = 0;
        CHECK_FALSE(truncated.read(later));
    }
}

TEST_CASE(".meta files round-trip, and hand edits are tolerated")
{
    AssetMeta model;
    model.id = {0x0123456789abcdefull};
    model.type = AssetType::Model;
    model.model = {.scale = 0.01f, .generateLods = false, .lodLevels = 2, .lodReduction = 0.25f};
    const std::string modelJson = writeAssetMeta(model);
    CHECK(modelJson.find("\"id\": \"0123456789abcdef\"") != std::string::npos);
    CHECK(modelJson.find("colorSpace") == std::string::npos);
    CHECK(readAssetMeta(modelJson) == model);

    AssetMeta texture;
    texture.id = {5};
    texture.type = AssetType::Texture;
    texture.texture = {.colorSpace = ColorSpace::Linear, .generateMips = false, .maxSize = 512};
    CHECK(readAssetMeta(writeAssetMeta(texture)) == texture);

    const std::optional<AssetMeta> sparse = readAssetMeta(R"({"id":"00000000000000ff","type":"model"})");
    REQUIRE(sparse);
    CHECK(sparse->id.value == 0xff);
    CHECK(sparse->model == ModelImportSettings{});

    const std::optional<AssetMeta> clamped =
        readAssetMeta(R"({"id":"00000000000000ff","type":"model","settings":{"scale":-3,"lodLevels":9,"lodReduction":2}})");
    REQUIRE(clamped);
    CHECK(clamped->model.scale == 1.0f);
    CHECK(clamped->model.lodLevels == kMaxLodLevels);
    CHECK(clamped->model.lodReduction == doctest::Approx(0.95f));

    CHECK_FALSE(readAssetMeta("not json"));
    CHECK_FALSE(readAssetMeta(R"({"type":"model"})"));
    CHECK_FALSE(readAssetMeta(R"({"id":"0000000000000000","type":"model"})"));
    CHECK_FALSE(readAssetMeta(R"({"id":"00000000000000ff","type":"sound"})"));
    CHECK_FALSE(readAssetMeta(R"({"id":"00000000000000ff","type":"model","settings":{"scale":"big"}})"));

    AssetMeta changed = model;
    CHECK(settingsHash(changed) == settingsHash(model));
    changed.model.lodLevels = 1;
    CHECK(settingsHash(changed) != settingsHash(model));
    changed = model;
    changed.texture.maxSize = 64;
    CHECK(settingsHash(changed) == settingsHash(model));
    changed.id = {6};
    CHECK(settingsHash(changed) == settingsHash(model));
}

TEST_CASE("Asset types and path ids")
{
    CHECK(assetTypeOf("assets://Models/a.GLB") == AssetType::Model);
    CHECK(assetTypeOf("assets://a.gltf") == AssetType::Model);
    CHECK(assetTypeOf("assets://a.jpeg") == AssetType::Texture);
    CHECK(assetTypeOf("assets://a.txt") == AssetType::Unknown);
    CHECK(assetTypeOf("assets://a.glb.meta") == AssetType::Unknown);
    CHECK(pathAssetId("assets://a.glb") == pathAssetId("assets://a.glb"));
    CHECK(pathAssetId("assets://a.glb") != pathAssetId("assets://b.glb"));
    CHECK_FALSE(pathAssetId("assets://a.glb").isNull());
    CHECK(metaPathOf("assets://a.glb") == "assets://a.glb.meta");
}

TEST_CASE("Cooked models round-trip and are validated")
{
    const ModelData original = sampleModel();
    const std::vector<std::byte> file = writeCookedModel(original, kHeader);

    CookedHeader header;
    const std::optional<ModelData> read = readCookedModel(file, &header);
    REQUIRE(read);
    CHECK(header.stamp == kHeader.stamp);
    CHECK(header.source == kHeader.source);
    CHECK(read->name == "Sample");
    CHECK(read->vertices.size() == 4);
    CHECK(read->vertices[3].uv.x == 1.0f);
    CHECK(read->vertices[3].tangent.w == original.vertices[3].tangent.w);
    CHECK(read->indices == original.indices);
    REQUIRE(read->primitives.size() == 1);
    CHECK(read->primitives[0].lodCount == 1);
    CHECK(read->primitives[0].lods[0].error == 0.25f);
    CHECK(read->primitives[0].bounds.max.y == 1.0f);
    CHECK(read->meshes[0].name == "Quad");
    CHECK(read->instances[0].world.column(3).x == 2.0f);
    CHECK(read->nodes[0].translation.x == 2.0f);
    CHECK(read->materials[0].alphaMode == AlphaMode::Blend);
    CHECK(read->materials[0].doubleSided);
    CHECK(read->materials[0].baseColorFactor.w == 0.5f);
    CHECK(read->textures[0].sampler.addressU == rhi::AddressMode::ClampToEdge);
    CHECK(read->textures[0].sampler.maxAnisotropy == 8);
    CHECK(read->materials[0].metallicRoughnessTexture == 1);
    CHECK(read->materials[0].metallicFactor == 0.25f);
    CHECK(read->materials[0].normalTexture == 1);
    CHECK(read->materials[0].normalScale == 0.5f);
    CHECK(read->materials[0].occlusionTexture == 1);
    CHECK(read->materials[0].occlusionStrength == 0.75f);
    CHECK(read->materials[0].emissiveTexture == 0);
    CHECK(read->materials[0].emissiveFactor.x == 2.0f);
    REQUIRE(read->imageMips.size() == 2);
    CHECK(read->imageMips[0].size() == original.imageMips[0].size());
    CHECK(read->imageMips[0][0].pixels == original.imageMips[0][0].pixels);
    CHECK(read->imageColorSpaces == original.imageColorSpaces);
    CHECK(read->images.size() == 2);
    CHECK(read->bounds.max.x == 3.0f);

    const std::optional<CookedHeader> onlyHeader =
        readCookedHeader(std::span(file).first(kCookedHeaderSize), AssetType::Model);
    REQUIRE(onlyHeader);
    CHECK(onlyHeader->stamp == kHeader.stamp);
    CHECK_FALSE(readCookedHeader(file, AssetType::Texture));

    std::vector<std::byte> touched = file;
    setCookedSource(touched, {123, 456});
    REQUIRE(readCookedModel(touched, &header));
    CHECK(header.source == SourceFingerprint{123, 456});
    CHECK(header.stamp == kHeader.stamp);

    SUBCASE("every truncation fails")
    {
        for (size_t size = 0; size < file.size(); size += 13)
            CHECK_FALSE(readCookedModel(std::span(file).first(size)));
    }

    SUBCASE("indices past their primitive's vertices fail")
    {
        ModelData broken = original;
        broken.indices[1] = 4;
        CHECK_FALSE(readCookedModel(writeCookedModel(broken, kHeader)));
    }

    SUBCASE("a level of detail past the indices fails")
    {
        ModelData broken = original;
        broken.primitives[0].lods[0].firstIndex = 8;
        CHECK_FALSE(readCookedModel(writeCookedModel(broken, kHeader)));
    }

    SUBCASE("a texture pointing at a missing image fails")
    {
        ModelData broken = original;
        broken.textures[0].image = 2;
        CHECK_FALSE(readCookedModel(writeCookedModel(broken, kHeader)));
    }

    SUBCASE("material textures out of range fail")
    {
        for (int32_t MaterialData::*slot : {&MaterialData::metallicRoughnessTexture, &MaterialData::normalTexture,
                                            &MaterialData::occlusionTexture, &MaterialData::emissiveTexture})
        {
            ModelData broken = original;
            broken.materials[0].*slot = 2;
            CHECK_FALSE(readCookedModel(writeCookedModel(broken, kHeader)));
        }
    }

    SUBCASE("mip levels of the wrong size fail")
    {
        ModelData broken = original;
        broken.imageMips[0][1] = broken.imageMips[0][0];
        CHECK_FALSE(readCookedModel(writeCookedModel(broken, kHeader)));
    }
}

TEST_CASE("Cooked textures round-trip")
{
    Image base;
    base.size = {8, 4};
    base.pixels.assign(8 * 4 * 4, std::byte{10});
    CookedTexture texture{.name = "Grid", .colorSpace = ColorSpace::Linear, .levels = generateMipChain(base, ColorSpace::Linear)};
    const std::vector<std::byte> file = writeCookedTexture(texture, kHeader);
    CookedHeader header;
    const std::optional<CookedTexture> read = readCookedTexture(file, &header);
    REQUIRE(read);
    CHECK(header.stamp == kHeader.stamp);
    CHECK(read->name == "Grid");
    CHECK(read->colorSpace == ColorSpace::Linear);
    REQUIRE(read->levels.size() == 4);
    CHECK(read->levels[3].size.width == 1);
    CHECK_FALSE(readCookedModel(file));
    CHECK_FALSE(readCookedTexture(std::span(file).first(file.size() - 1)));
}

TEST_CASE("Importers apply their settings")
{
    SUBCASE("textures are reduced to the maximum size, with or without mips")
    {
        const std::vector<std::byte> bmp = makeBmp(16, 8, 255);
        const std::optional<CookedTexture> full = importTextureAsset(bmp, "a.bmp", {});
        REQUIRE(full);
        CHECK(full->levels.size() == 5);
        CHECK(full->levels[0].size.width == 16);

        TextureImportSettings settings;
        settings.colorSpace = ColorSpace::Linear;
        settings.generateMips = false;
        settings.maxSize = 4;
        const std::optional<CookedTexture> small = importTextureAsset(bmp, "a.bmp", settings);
        REQUIRE(small);
        REQUIRE(small->levels.size() == 1);
        CHECK(small->levels[0].size.width == 4);
        CHECK(small->levels[0].size.height == 2);
        CHECK(small->colorSpace == ColorSpace::Linear);

        CHECK_FALSE(importTextureAsset(bytesOf("not an image"), "a.bmp", {}));
    }

    SUBCASE("models are scaled about their origin")
    {
        const std::string gltf = triangleGltf();
        ModelImportSettings settings;
        settings.scale = 2.0f;
        settings.generateLods = false;
        const std::optional<ModelData> model = importModelAsset(bytesOf(gltf), "test.gltf", settings);
        REQUIRE(model);
        CHECK(model->name == "test.gltf");
        REQUIRE(model->nodes.size() == 1);
        CHECK(model->nodes[0].translation.y == doctest::Approx(4.0f));
        CHECK(model->nodes[0].scale.x == doctest::Approx(2.0f));
        CHECK(model->instances[0].world.column(3).z == doctest::Approx(6.0f));
        CHECK(model->bounds.max.x == doctest::Approx(4.0f));
        CHECK(model->vertices[1].position.x == 1.0f);
        CHECK(model->primitives[0].lodCount == 0);
    }

    SUBCASE("the number of levels of detail is limited")
    {
        ModelData grid;
        constexpr uint32_t kSide = 24;
        for (uint32_t y = 0; y <= kSide; ++y)
            for (uint32_t x = 0; x <= kSide; ++x)
                grid.vertices.push_back({{static_cast<float>(x), std::sin(static_cast<float>(x + y) * 0.7f), static_cast<float>(y)}, {0, 1, 0}, {0, 0}});
        for (uint32_t y = 0; y < kSide; ++y)
            for (uint32_t x = 0; x < kSide; ++x)
            {
                const uint32_t i = y * (kSide + 1) + x;
                grid.indices.insert(grid.indices.end(), {i, i + kSide + 1, i + 1, i + 1, i + kSide + 1, i + kSide + 2});
            }
        Primitive primitive;
        primitive.indexCount = static_cast<uint32_t>(grid.indices.size());
        primitive.vertexCount = static_cast<uint32_t>(grid.vertices.size());
        grid.primitives = {primitive};

        LodSettings settings;
        settings.maxLevels = 1;
        generateLods(grid, settings);
        CHECK(grid.primitives[0].lodCount == 1);
        settings.maxLevels = kMaxLodLevels;
        generateLods(grid, settings);
        CHECK(grid.primitives[0].lodCount > 1);
    }
}

TEST_CASE("The asset database gives assets lasting ids and cooks them on demand")
{
    Project project;
    project.assets->add("Textures/grid.bmp", makeBmp(8, 8, 10));
    project.assets->add("Models/tri.gltf", triangleGltf());
    project.assets->add("readme.txt", "not an asset");

    AssetDatabase database(project.fileSystem);
    CHECK(database.refresh() == 2);
    const AssetRecord* texture = database.findByPath("assets://Textures/grid.bmp");
    const AssetRecord* model = database.findByPath("assets:Models//tri.gltf");
    REQUIRE(texture);
    REQUIRE(model);
    CHECK(texture->metaSaved);
    CHECK(texture->meta.type == AssetType::Texture);
    CHECK(model->meta.type == AssetType::Model);
    CHECK(texture->meta.id != model->meta.id);
    CHECK(texture->state == AssetState::NotImported);
    CHECK(project.fileSystem.isFile("assets://Textures/grid.bmp.meta"));
    const AssetId textureId = texture->meta.id;
    const AssetId modelId = model->meta.id;
    CHECK(database.records().size() == 2);

    // Another run reads the same ids back.
    AssetDatabase again(project.fileSystem);
    again.refresh();
    REQUIRE(again.findByPath("assets://Textures/grid.bmp"));
    CHECK(again.findByPath("assets://Textures/grid.bmp")->meta.id == textureId);

    const std::optional<CookedTexture> cooked = database.loadTexture(textureId);
    REQUIRE(cooked);
    CHECK(cooked->levels.size() == 4);
    CHECK(database.find(textureId)->state == AssetState::UpToDate);
    const std::string cookedPath = database.cookedPath(*database.find(textureId));
    CHECK(cookedPath == "library://Textures/" + textureId.toString() + ".arctex");
    CHECK(project.fileSystem.isFile(cookedPath));
    CHECK(database.find(modelId)->state == AssetState::NotImported);

    SUBCASE("a changed source is imported again")
    {
        project.assets->add("Textures/grid.bmp", makeBmp(8, 8, 200));
        database.refresh();
        CHECK(database.find(textureId)->state == AssetState::Stale);
        const std::optional<CookedTexture> reloaded = database.loadTexture(textureId);
        REQUIRE(reloaded);
        CHECK(reloaded->levels[0].pixels[0] != cooked->levels[0].pixels[0]);
        CHECK(database.find(textureId)->state == AssetState::UpToDate);
    }

    SUBCASE("a source rewritten with the same bytes stays current")
    {
        project.assets->add("Textures/grid.bmp", makeBmp(8, 8, 10));
        database.refresh();
        CHECK(database.find(textureId)->state == AssetState::UpToDate);
    }

    SUBCASE("new settings are saved and make the cooked copy stale")
    {
        TextureImportSettings settings;
        settings.generateMips = false;
        REQUIRE(database.setSettings(textureId, {}, settings));
        CHECK(database.find(textureId)->state == AssetState::Stale);
        const std::optional<std::string> meta = project.fileSystem.readText("assets://Textures/grid.bmp.meta");
        REQUIRE(meta);
        CHECK(meta->find("\"generateMips\": false") != std::string::npos);
        const std::optional<CookedTexture> single = database.loadTexture(textureId);
        REQUIRE(single);
        CHECK(single->levels.size() == 1);
    }

    SUBCASE("a damaged cooked copy is rebuilt")
    {
        std::optional<std::vector<std::byte>> file = project.fileSystem.read(cookedPath);
        REQUIRE(file);
        file->resize(file->size() / 2);
        project.fileSystem.write(cookedPath, *file);
        const std::optional<CookedTexture> rebuilt = database.loadTexture(textureId);
        REQUIRE(rebuilt);
        CHECK(rebuilt->levels.size() == 4);
    }

    SUBCASE("models cook too")
    {
        const std::optional<ModelData> data = database.loadModel(modelId);
        REQUIRE(data);
        CHECK(data->vertices.size() == 3);
        CHECK(database.find(modelId)->state == AssetState::UpToDate);
        CHECK_FALSE(database.loadModel(textureId));
        CHECK(database.importAll() == 0);
        CHECK(database.importAll(true) == 2);
    }

    SUBCASE("a removed source is missing, and its .meta file is reported")
    {
        project.assets->remove("Textures/grid.bmp");
        database.refresh();
        CHECK_FALSE(database.findByPath("assets://Textures/grid.bmp"));
        REQUIRE(database.orphanedMetas().size() == 1);
        CHECK(database.orphanedMetas()[0] == "assets://Textures/grid.bmp.meta");
    }
}

TEST_CASE("Assets without a writable folder get ids from their path")
{
    VirtualFileSystem fileSystem;
    auto assets = std::make_shared<MemorySource>(false);
    assets->add("grid.bmp", makeBmp(4, 4, 1));
    fileSystem.mount("assets", assets);
    fileSystem.mount("library", std::make_shared<MemorySource>(true));

    AssetDatabase database(fileSystem);
    database.refresh();
    const AssetRecord* record = database.findByPath("assets://grid.bmp");
    REQUIRE(record);
    CHECK_FALSE(record->metaSaved);
    CHECK(record->meta.id == pathAssetId("assets://grid.bmp"));
    CHECK_FALSE(fileSystem.exists("assets://grid.bmp.meta"));
    CHECK(database.loadTexture(record->meta.id));
}

TEST_CASE("A copied .meta file does not give two assets the same id")
{
    Project project;
    project.assets->add("a.bmp", makeBmp(4, 4, 1));
    project.assets->add("b.bmp", makeBmp(4, 4, 2));
    AssetMeta meta;
    meta.id = {77};
    meta.type = AssetType::Texture;
    project.assets->add("a.bmp.meta", writeAssetMeta(meta));
    project.assets->add("b.bmp.meta", writeAssetMeta(meta));

    AssetDatabase database(project.fileSystem);
    database.refresh();
    const AssetRecord* a = database.findByPath("assets://a.bmp");
    const AssetRecord* b = database.findByPath("assets://b.bmp");
    REQUIRE(a);
    REQUIRE(b);
    CHECK(a->meta.id.value == 77);
    CHECK(b->meta.id != a->meta.id);
    const std::optional<std::string> rewritten = project.fileSystem.readText("assets://b.bmp.meta");
    REQUIRE(rewritten);
    CHECK(rewritten->find(b->meta.id.toString()) != std::string::npos);
}

TEST_CASE("Files outside the asset root can be registered")
{
    Project project;
    auto other = std::make_shared<MemorySource>(true);
    other->add("dropped.bmp", makeBmp(4, 4, 3));
    project.fileSystem.mount("other", other);

    AssetDatabase database(project.fileSystem);
    database.refresh();
    CHECK_FALSE(database.findByPath("other://dropped.bmp"));
    const AssetId id = database.registerPath("other://dropped.bmp");
    CHECK_FALSE(id.isNull());
    CHECK(database.registerPath("other:/dropped.bmp") == id);
    CHECK(project.fileSystem.isFile("other://dropped.bmp.meta"));
    CHECK(database.registerPath("other://missing.bmp").isNull());
    CHECK(database.registerPath("other://dropped.bmp.meta").isNull());

    // Kept through a refresh, which only scans the asset root.
    database.refresh();
    CHECK(database.find(id));
}

TEST_CASE("The resource manager shares what it loads and lets it go with its last owner")
{
    Project project;
    project.assets->add("grid.bmp", makeBmp(8, 8, 10));
    project.assets->add("tri.gltf", triangleGltf());
    AssetDatabase database(project.fileSystem);
    database.refresh();

    auto device = rhi::createNullDevice({});
    REQUIRE(device);
    GeometryPool geometry(*device);
    ResourceManager resources(database, *device, geometry);

    std::shared_ptr<const TextureAsset> first = resources.loadTexture("assets://grid.bmp");
    REQUIRE(first.get());
    REQUIRE(first->texture);
    CHECK(first->texture->desc().mipLevels == 4);
    std::shared_ptr<const TextureAsset> second = resources.loadTexture(first->asset);
    CHECK(second.get() == first.get());
    REQUIRE(resources.loaded().size() == 1);
    CHECK(resources.loaded()[0].references == 2);
    CHECK(resources.loaded()[0].path == "assets://grid.bmp");

    second.reset();
    CHECK(resources.loaded()[0].references == 1);
    const std::weak_ptr<const TextureAsset> weak = first;
    first.reset();
    CHECK(weak.expired());
    CHECK(resources.loaded().empty());
    CHECK(resources.loadTexture("assets://grid.bmp").get() != nullptr);

    const std::shared_ptr<const Model> model = resources.loadModel("assets://tri.gltf");
    REQUIRE(model.get());
    CHECK(model->asset() == database.findByPath("assets://tri.gltf")->meta.id);
    CHECK(model->triangleCount() == 1);
    CHECK(resources.loadedModels().size() == 1);
    CHECK(resources.loadModel("assets://grid.bmp").get() == nullptr);
    CHECK(resources.loadModel("assets://nothing.gltf").get() == nullptr);
}

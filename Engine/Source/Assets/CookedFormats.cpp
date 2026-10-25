#include "Assets/CookedFormats.h"

#include "Core/BinaryStream.h"
#include "RHI/RHITypes.h"

#include <cstring>
#include <type_traits>

namespace arc
{
    namespace
    {
        // "ARCM" and "ARCT" as they appear in the file.
        constexpr uint32_t kModelMagic = 0x4D435241;
        constexpr uint32_t kTextureMagic = 0x54435241;
        // Bumped whenever the layout below changes; older files are then rebuilt, not misread.
        constexpr uint32_t kFormatVersion = 2;

        // Limits on counts read from a file, so a corrupt count fails instead of allocating gigabytes.
        constexpr uint32_t kMaxVertices = 1u << 26;
        constexpr uint32_t kMaxIndices = 1u << 28;
        constexpr uint32_t kMaxElements = 1u << 20;
        constexpr uint32_t kMaxMipLevels = 16;
        constexpr uint32_t kMaxImageSize = 16384;

        static_assert(std::is_trivially_copyable_v<MeshVertex> && std::is_trivially_copyable_v<Mat4> &&
                      std::is_trivially_copyable_v<Quat> && std::is_trivially_copyable_v<Aabb>);

        // Where the fingerprint starts: after magic, version, importer version, id and two hashes.
        constexpr size_t kSourceOffset = 36;
        static_assert(kSourceOffset + sizeof(uint64_t) + sizeof(int64_t) == kCookedHeaderSize);

        void writeHeader(BinaryWriter& writer, uint32_t magic, const CookedHeader& header)
        {
            writer.write(magic);
            writer.write(kFormatVersion);
            writer.write(header.stamp.importerVersion);
            writer.write(header.stamp.id.value);
            writer.write(header.stamp.sourceHash);
            writer.write(header.stamp.settingsHash);
            writer.write(header.source.size);
            writer.write(header.source.modifiedTime);
        }

        bool readHeader(BinaryReader& reader, uint32_t expectedMagic, CookedHeader& header)
        {
            uint32_t magic = 0;
            uint32_t version = 0;
            CookedStamp& stamp = header.stamp;
            return reader.read(magic) && magic == expectedMagic && reader.read(version) && version == kFormatVersion &&
                   reader.read(stamp.importerVersion) && reader.read(stamp.id.value) && reader.read(stamp.sourceHash) &&
                   reader.read(stamp.settingsHash) && reader.read(header.source.size) &&
                   reader.read(header.source.modifiedTime);
        }

        void writeImage(BinaryWriter& writer, const Image& image)
        {
            writer.write(image.size.width);
            writer.write(image.size.height);
            writer.writeArray(std::span<const std::byte>(image.pixels));
        }

        bool readImage(BinaryReader& reader, Image& image)
        {
            if (!reader.read(image.size.width) || !reader.read(image.size.height) || image.size.width == 0 ||
                image.size.height == 0 || image.size.width > kMaxImageSize || image.size.height > kMaxImageSize)
                return false;
            const uint64_t expected = uint64_t{image.size.width} * image.size.height * 4;
            return reader.readArray(image.pixels, static_cast<uint32_t>(expected)) && image.pixels.size() == expected;
        }

        void writeMipChain(BinaryWriter& writer, const std::vector<Image>& levels)
        {
            writer.write(static_cast<uint32_t>(levels.size()));
            for (const Image& level : levels)
                writeImage(writer, level);
        }

        // Every level must be the size createTexture() expects for its index.
        bool readMipChain(BinaryReader& reader, std::vector<Image>& levels)
        {
            if (!reader.readEach(levels, kMaxMipLevels, readImage))
                return false;
            for (size_t i = 1; i < levels.size(); ++i)
            {
                const Extent2D expected = rhi::mipSize(levels[0].size, static_cast<uint32_t>(i));
                if (levels[i].size.width != expected.width || levels[i].size.height != expected.height)
                    return false;
            }
            return true;
        }

        template <typename Enum>
        bool readEnum(BinaryReader& reader, Enum& value, uint8_t last);

        // Each image is its colour space, then its mip chain.
        bool readImages(BinaryReader& reader, ModelData& data)
        {
            uint32_t count = 0;
            if (!reader.read(count) || count > kMaxElements)
                return false;
            data.imageMips.resize(count);
            data.imageColorSpaces.resize(count);
            for (uint32_t i = 0; i < count; ++i)
                if (!readEnum(reader, data.imageColorSpaces[i], static_cast<uint8_t>(ColorSpace::Linear)) ||
                    !readMipChain(reader, data.imageMips[i]))
                    return false;
            return true;
        }

        template <typename Enum>
        bool readEnum(BinaryReader& reader, Enum& value, uint8_t last)
        {
            uint8_t raw = 0;
            if (!reader.read(raw) || raw > last)
                return false;
            value = static_cast<Enum>(raw);
            return true;
        }

        bool validIndexRange(uint32_t first, uint32_t count, size_t size)
        {
            return uint64_t{first} + count <= size;
        }

        // A cooked file is trusted no more than a source file: everything Model::create() and the
        // renderer index with is checked here.
        bool validate(const ModelData& data)
        {
            for (const Primitive& primitive : data.primitives)
            {
                if (!validIndexRange(primitive.baseVertex, primitive.vertexCount, data.vertices.size()) ||
                    !validIndexRange(primitive.firstIndex, primitive.indexCount, data.indices.size()) ||
                    primitive.lodCount > kMaxLodLevels || primitive.indexCount % 3 != 0 ||
                    primitive.material < -1 || primitive.material >= static_cast<int64_t>(data.materials.size()))
                    return false;

                const auto indicesInRange = [&](uint32_t first, uint32_t count) {
                    if (!validIndexRange(first, count, data.indices.size()))
                        return false;
                    for (uint32_t i = first; i < first + count; ++i)
                        if (data.indices[i] >= primitive.vertexCount)
                            return false;
                    return true;
                };
                if (!indicesInRange(primitive.firstIndex, primitive.indexCount))
                    return false;
                for (uint32_t level = 0; level < primitive.lodCount; ++level)
                    if (!indicesInRange(primitive.lods[level].firstIndex, primitive.lods[level].indexCount) ||
                        primitive.lods[level].indexCount % 3 != 0)
                        return false;
            }
            for (const MeshData& mesh : data.meshes)
                if (!validIndexRange(mesh.firstPrimitive, mesh.primitiveCount, data.primitives.size()))
                    return false;
            for (const MeshInstance& instance : data.instances)
                if (instance.mesh >= data.meshes.size())
                    return false;
            for (size_t i = 0; i < data.nodes.size(); ++i)
            {
                const NodeData& node = data.nodes[i];
                if (node.parent < -1 || node.parent >= static_cast<int64_t>(i) || node.mesh < -1 ||
                    node.mesh >= static_cast<int64_t>(data.meshes.size()))
                    return false;
            }
            const auto textureInRange = [&](int32_t texture) {
                return texture >= -1 && texture < static_cast<int64_t>(data.textures.size());
            };
            for (const MaterialData& material : data.materials)
                if (!textureInRange(material.baseColorTexture) || !textureInRange(material.metallicRoughnessTexture))
                    return false;
            for (const TextureData& texture : data.textures)
                if (texture.image < -1 || texture.image >= static_cast<int64_t>(data.imageMips.size()))
                    return false;
            return true;
        }
    }

    std::vector<std::byte> writeCookedModel(const ModelData& data, const CookedHeader& header)
    {
        BinaryWriter writer;
        writeHeader(writer, kModelMagic, header);
        writer.writeString(data.name);
        writer.writeArray(std::span<const MeshVertex>(data.vertices));
        writer.writeArray(std::span<const uint32_t>(data.indices));

        writer.write(static_cast<uint32_t>(data.primitives.size()));
        for (const Primitive& primitive : data.primitives)
        {
            writer.write(primitive.firstIndex);
            writer.write(primitive.indexCount);
            writer.write(primitive.baseVertex);
            writer.write(primitive.vertexCount);
            writer.write(primitive.material);
            writer.write(primitive.bounds);
            writer.write(primitive.lodCount);
            for (const LodLevel& level : primitive.lods)
            {
                writer.write(level.firstIndex);
                writer.write(level.indexCount);
                writer.write(level.error);
            }
        }

        writer.write(static_cast<uint32_t>(data.meshes.size()));
        for (const MeshData& mesh : data.meshes)
        {
            writer.writeString(mesh.name);
            writer.write(mesh.firstPrimitive);
            writer.write(mesh.primitiveCount);
        }

        writer.write(static_cast<uint32_t>(data.instances.size()));
        for (const MeshInstance& instance : data.instances)
        {
            writer.writeString(instance.name);
            writer.write(instance.world);
            writer.write(instance.mesh);
        }

        writer.write(static_cast<uint32_t>(data.nodes.size()));
        for (const NodeData& node : data.nodes)
        {
            writer.writeString(node.name);
            writer.write(node.translation);
            writer.write(node.rotation);
            writer.write(node.scale);
            writer.write(node.parent);
            writer.write(node.mesh);
        }

        writer.write(static_cast<uint32_t>(data.materials.size()));
        for (const MaterialData& material : data.materials)
        {
            writer.writeString(material.name);
            writer.write(material.baseColorFactor);
            writer.write(material.baseColorTexture);
            writer.write(material.metallicFactor);
            writer.write(material.roughnessFactor);
            writer.write(material.metallicRoughnessTexture);
            writer.write(static_cast<uint8_t>(material.alphaMode));
            writer.write(material.alphaCutoff);
            writer.write(static_cast<uint8_t>(material.doubleSided));
        }

        writer.write(static_cast<uint32_t>(data.textures.size()));
        for (const TextureData& texture : data.textures)
        {
            const rhi::SamplerDesc& sampler = texture.sampler;
            writer.write(texture.image);
            writer.write(static_cast<uint8_t>(sampler.minFilter));
            writer.write(static_cast<uint8_t>(sampler.magFilter));
            writer.write(static_cast<uint8_t>(sampler.mipFilter));
            writer.write(static_cast<uint8_t>(sampler.addressU));
            writer.write(static_cast<uint8_t>(sampler.addressV));
            writer.write(static_cast<uint8_t>(sampler.addressW));
            writer.write(static_cast<uint8_t>(sampler.borderColor));
            writer.write(sampler.maxAnisotropy);
            writer.write(sampler.minLod);
            writer.write(sampler.maxLod);
        }

        writer.write(static_cast<uint32_t>(data.imageMips.size()));
        for (size_t i = 0; i < data.imageMips.size(); ++i)
        {
            const ColorSpace colorSpace = i < data.imageColorSpaces.size() ? data.imageColorSpaces[i] : ColorSpace::Srgb;
            writer.write(static_cast<uint8_t>(colorSpace));
            writeMipChain(writer, data.imageMips[i]);
        }

        writer.write(data.bounds);
        return writer.take();
    }

    std::optional<ModelData> readCookedModel(std::span<const std::byte> file, CookedHeader* headerOut)
    {
        BinaryReader reader(file);
        CookedHeader header;
        ModelData data;
        if (!readHeader(reader, kModelMagic, header) || !reader.readString(data.name) ||
            !reader.readArray(data.vertices, kMaxVertices) || !reader.readArray(data.indices, kMaxIndices))
            return std::nullopt;

        const bool read =
            reader.readEach(data.primitives, kMaxElements,
                            [](BinaryReader& r, Primitive& primitive) {
                                bool ok = r.read(primitive.firstIndex) && r.read(primitive.indexCount) &&
                                          r.read(primitive.baseVertex) && r.read(primitive.vertexCount) &&
                                          r.read(primitive.material) && r.read(primitive.bounds) && r.read(primitive.lodCount);
                                for (LodLevel& level : primitive.lods)
                                    ok = ok && r.read(level.firstIndex) && r.read(level.indexCount) && r.read(level.error);
                                return ok;
                            }) &&
            reader.readEach(data.meshes, kMaxElements,
                            [](BinaryReader& r, MeshData& mesh) {
                                return r.readString(mesh.name) && r.read(mesh.firstPrimitive) && r.read(mesh.primitiveCount);
                            }) &&
            reader.readEach(data.instances, kMaxElements,
                            [](BinaryReader& r, MeshInstance& instance) {
                                return r.readString(instance.name) && r.read(instance.world) && r.read(instance.mesh);
                            }) &&
            reader.readEach(data.nodes, kMaxElements,
                            [](BinaryReader& r, NodeData& node) {
                                return r.readString(node.name) && r.read(node.translation) && r.read(node.rotation) &&
                                       r.read(node.scale) && r.read(node.parent) && r.read(node.mesh);
                            }) &&
            reader.readEach(data.materials, kMaxElements,
                            [](BinaryReader& r, MaterialData& material) {
                                uint8_t doubleSided = 0;
                                const bool ok = r.readString(material.name) && r.read(material.baseColorFactor) &&
                                                r.read(material.baseColorTexture) && r.read(material.metallicFactor) &&
                                                r.read(material.roughnessFactor) && r.read(material.metallicRoughnessTexture) &&
                                                readEnum(r, material.alphaMode, static_cast<uint8_t>(AlphaMode::Blend)) &&
                                                r.read(material.alphaCutoff) && r.read(doubleSided) && doubleSided <= 1;
                                material.doubleSided = doubleSided != 0;
                                return ok;
                            }) &&
            reader.readEach(data.textures, kMaxElements,
                            [](BinaryReader& r, TextureData& texture) {
                                rhi::SamplerDesc& sampler = texture.sampler;
                                constexpr auto lastFilter = static_cast<uint8_t>(rhi::Filter::Linear);
                                constexpr auto lastAddress = static_cast<uint8_t>(rhi::AddressMode::ClampToBorder);
                                constexpr auto lastBorder = static_cast<uint8_t>(rhi::BorderColor::OpaqueWhite);
                                return r.read(texture.image) && readEnum(r, sampler.minFilter, lastFilter) &&
                                       readEnum(r, sampler.magFilter, lastFilter) && readEnum(r, sampler.mipFilter, lastFilter) &&
                                       readEnum(r, sampler.addressU, lastAddress) && readEnum(r, sampler.addressV, lastAddress) &&
                                       readEnum(r, sampler.addressW, lastAddress) &&
                                       readEnum(r, sampler.borderColor, lastBorder) && r.read(sampler.maxAnisotropy) &&
                                       sampler.maxAnisotropy >= 1 && sampler.maxAnisotropy <= 16 && r.read(sampler.minLod) &&
                                       r.read(sampler.maxLod);
                            }) &&
            readImages(reader, data) && reader.read(data.bounds) && reader.atEnd();
        if (!read || !validate(data))
            return std::nullopt;

        data.images.resize(data.imageMips.size());
        if (headerOut)
            *headerOut = header;
        return data;
    }

    std::vector<std::byte> writeCookedTexture(const CookedTexture& texture, const CookedHeader& header)
    {
        BinaryWriter writer;
        writeHeader(writer, kTextureMagic, header);
        writer.writeString(texture.name);
        writer.write(static_cast<uint8_t>(texture.colorSpace));
        writeMipChain(writer, texture.levels);
        return writer.take();
    }

    std::optional<CookedTexture> readCookedTexture(std::span<const std::byte> file, CookedHeader* headerOut)
    {
        BinaryReader reader(file);
        CookedHeader header;
        CookedTexture texture;
        if (!readHeader(reader, kTextureMagic, header) || !reader.readString(texture.name) ||
            !readEnum(reader, texture.colorSpace, static_cast<uint8_t>(ColorSpace::Linear)) ||
            !readMipChain(reader, texture.levels) || texture.levels.empty() || !reader.atEnd())
            return std::nullopt;
        if (headerOut)
            *headerOut = header;
        return texture;
    }

    std::optional<CookedHeader> readCookedHeader(std::span<const std::byte> file, AssetType type)
    {
        BinaryReader reader(file);
        CookedHeader header;
        const uint32_t magic = type == AssetType::Model ? kModelMagic : kTextureMagic;
        if (type == AssetType::Unknown || !readHeader(reader, magic, header))
            return std::nullopt;
        return header;
    }

    void setCookedSource(std::vector<std::byte>& file, const SourceFingerprint& source)
    {
        if (file.size() < kCookedHeaderSize)
            return;
        std::memcpy(file.data() + kSourceOffset, &source.size, sizeof(source.size));
        std::memcpy(file.data() + kSourceOffset + sizeof(source.size), &source.modifiedTime, sizeof(source.modifiedTime));
    }
}

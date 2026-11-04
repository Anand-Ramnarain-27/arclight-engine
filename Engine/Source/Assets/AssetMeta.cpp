#include "Assets/AssetMeta.h"

#include "Core/Hash.h"
#include "Platform/VirtualFileSystem.h"

// nlohmann/json, as vendored by tinygltf.
#include <json.hpp>

#include <algorithm>
#include <bit>

namespace arc
{
    namespace
    {
        constexpr int kMetaVersion = 1;

        const char* colorSpaceName(ColorSpace colorSpace)
        {
            return colorSpace == ColorSpace::Linear ? "linear" : "srgb";
        }

        const char* compressionKey(TextureCompression compression)
        {
            switch (compression)
            {
                case TextureCompression::None:    return "none";
                case TextureCompression::Quality: return "quality";
                case TextureCompression::Small:   return "small";
            }
            return "quality";
        }

        // An unknown name keeps the default rather than failing the whole file.
        TextureCompression compressionFromKey(const std::string& key, TextureCompression fallback)
        {
            for (const TextureCompression compression :
                 {TextureCompression::None, TextureCompression::Quality, TextureCompression::Small})
                if (key == compressionKey(compression))
                    return compression;
            return fallback;
        }

        template <typename T>
        uint64_t hashValue(uint64_t hash, const T& value)
        {
            return fnv1a64(std::as_bytes(std::span(&value, 1)), hash);
        }
    }

    const char* assetTypeName(AssetType type)
    {
        switch (type)
        {
            case AssetType::Model:   return "model";
            case AssetType::Texture: return "texture";
            case AssetType::Unknown: break;
        }
        return "unknown";
    }

    AssetType assetTypeOf(std::string_view path)
    {
        const std::string extension = extensionOf(path);
        if (extension == ".gltf" || extension == ".glb")
            return AssetType::Model;
        if (extension == ".png" || extension == ".jpg" || extension == ".jpeg" || extension == ".tga" || extension == ".bmp")
            return AssetType::Texture;
        return AssetType::Unknown;
    }

    AssetId pathAssetId(std::string_view path)
    {
        // Seeded so a path id can never equal the hash of the same text used for something else.
        const uint64_t value = fnv1a64(path, fnv1a64("arclight-asset-path:"));
        return {value != 0 ? value : 1};
    }

    std::string metaPathOf(std::string_view sourcePath)
    {
        return std::string(sourcePath) + ".meta";
    }

    std::string writeAssetMeta(const AssetMeta& meta)
    {
        nlohmann::ordered_json json;
        json["version"] = kMetaVersion;
        json["id"] = meta.id.toString();
        json["type"] = assetTypeName(meta.type);
        nlohmann::ordered_json& settings = json["settings"];
        settings = nlohmann::ordered_json::object();
        if (meta.type == AssetType::Model)
        {
            settings["scale"] = meta.model.scale;
            settings["generateLods"] = meta.model.generateLods;
            settings["lodLevels"] = meta.model.lodLevels;
            settings["lodReduction"] = meta.model.lodReduction;
            settings["textureCompression"] = compressionKey(meta.model.textureCompression);
        }
        else if (meta.type == AssetType::Texture)
        {
            settings["colorSpace"] = colorSpaceName(meta.texture.colorSpace);
            settings["generateMips"] = meta.texture.generateMips;
            settings["maxSize"] = meta.texture.maxSize;
            settings["compression"] = compressionKey(meta.texture.compression);
        }
        return json.dump(2) + "\n";
    }

    std::optional<AssetMeta> readAssetMeta(std::string_view text)
    {
        const nlohmann::json json = nlohmann::json::parse(text.begin(), text.end(), nullptr, false);
        if (json.is_discarded() || !json.is_object())
            return std::nullopt;

        // value() throws when a key holds the wrong type; a hand-edited file must not crash the editor.
        try
        {
            AssetMeta meta;
            const std::optional<Uuid> id = Uuid::fromString(json.value("id", std::string()));
            if (!id || id->isNull())
                return std::nullopt;
            meta.id = *id;

            const std::string type = json.value("type", std::string());
            if (type == "model")
                meta.type = AssetType::Model;
            else if (type == "texture")
                meta.type = AssetType::Texture;
            else
                return std::nullopt;

            const nlohmann::json settings = json.value("settings", nlohmann::json::object());
            if (!settings.is_object())
                return meta;

            if (meta.type == AssetType::Model)
            {
                ModelImportSettings& model = meta.model;
                model.scale = settings.value("scale", model.scale);
                if (!(model.scale > 0.0f))
                    model.scale = 1.0f;
                model.generateLods = settings.value("generateLods", model.generateLods);
                model.lodLevels = std::min(settings.value("lodLevels", model.lodLevels), kMaxLodLevels);
                model.lodReduction = std::clamp(settings.value("lodReduction", model.lodReduction), 0.05f, 0.95f);
                model.textureCompression =
                    compressionFromKey(settings.value("textureCompression", std::string()), model.textureCompression);
            }
            else
            {
                TextureImportSettings& texture = meta.texture;
                texture.colorSpace = settings.value("colorSpace", std::string("srgb")) == "linear" ? ColorSpace::Linear
                                                                                                    : ColorSpace::Srgb;
                texture.generateMips = settings.value("generateMips", texture.generateMips);
                texture.maxSize = std::clamp(settings.value("maxSize", texture.maxSize), 1u, 16384u);
                texture.compression = compressionFromKey(settings.value("compression", std::string()), texture.compression);
            }
            return meta;
        }
        catch (const nlohmann::json::exception&)
        {
            return std::nullopt;
        }
    }

    uint64_t settingsHash(const AssetMeta& meta)
    {
        uint64_t hash = hashValue(kFnv1aBasis, meta.type);
        if (meta.type == AssetType::Model)
        {
            hash = hashValue(hash, std::bit_cast<uint32_t>(meta.model.scale));
            hash = hashValue(hash, meta.model.generateLods);
            hash = hashValue(hash, meta.model.lodLevels);
            hash = hashValue(hash, std::bit_cast<uint32_t>(meta.model.lodReduction));
            hash = hashValue(hash, meta.model.textureCompression);
        }
        else if (meta.type == AssetType::Texture)
        {
            hash = hashValue(hash, meta.texture.colorSpace);
            hash = hashValue(hash, meta.texture.generateMips);
            hash = hashValue(hash, meta.texture.maxSize);
            hash = hashValue(hash, meta.texture.compression);
        }
        return hash;
    }
}

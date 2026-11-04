#pragma once

#include "Renderer/Image.h"
#include "Renderer/ModelData.h"
#include "Renderer/TextureEncoding.h"
#include "Scene/Uuid.h"

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace arc
{
    /// Identifies an asset for as long as it exists, whatever it is renamed to or wherever it is
    /// moved, as long as its .meta file moves with it.
    using AssetId = Uuid;

    enum class AssetType : uint8_t
    {
        Unknown,
        Model,
        Texture,
    };

    [[nodiscard]] const char* assetTypeName(AssetType type);

    /// What a source file is imported as, from its extension; Unknown for files that are not assets.
    [[nodiscard]] AssetType assetTypeOf(std::string_view path);

    /// An id derived from the path, for assets that cannot have a .meta file written next to them
    /// (a read-only folder, or a file outside every mount). The same path always gives the same id.
    [[nodiscard]] AssetId pathAssetId(std::string_view path);

    struct ModelImportSettings
    {
        /// Applied to the model's root nodes, so 0.01 imports a model authored in centimetres.
        float scale = 1.0f;
        bool generateLods = true;
        /// At most kMaxLodLevels.
        uint32_t lodLevels = kMaxLodLevels;
        /// Each level aims for this fraction of the previous level's triangles.
        float lodReduction = 0.5f;
        /// How the model's images are cooked; each image's material slot decides which format.
        TextureCompression textureCompression = TextureCompression::Quality;

        bool operator==(const ModelImportSettings&) const = default;
    };

    struct TextureImportSettings
    {
        ColorSpace colorSpace = ColorSpace::Srgb;
        bool generateMips = true;
        /// Larger images are halved until both sides fit. Pixels.
        uint32_t maxSize = 4096;
        TextureCompression compression = TextureCompression::Quality;

        bool operator==(const TextureImportSettings&) const = default;
    };

    /// The contents of an asset's .meta file, which sits next to the source file and is committed
    /// with it: the id other files refer to the asset by, and how to import it. Whether the cooked
    /// copy is current is recorded in the cooked file instead, so importing never changes the
    /// .meta file and never shows up as a change in version control.
    struct AssetMeta
    {
        AssetId id;
        AssetType type = AssetType::Unknown;
        ModelImportSettings model;
        TextureImportSettings texture;

        bool operator==(const AssetMeta&) const = default;
    };

    /// The .meta file of `sourcePath`: the same path with ".meta" appended.
    [[nodiscard]] std::string metaPathOf(std::string_view sourcePath);

    /// JSON, with only the settings that apply to the asset's type.
    [[nodiscard]] std::string writeAssetMeta(const AssetMeta& meta);

    /// Fails on malformed JSON, an unknown type or a missing or invalid id. Settings that are
    /// missing keep their defaults, and out-of-range ones are clamped, so a hand-edited file loads.
    [[nodiscard]] std::optional<AssetMeta> readAssetMeta(std::string_view json);

    /// Changes whenever a setting that affects the import of `meta.type` changes.
    [[nodiscard]] uint64_t settingsHash(const AssetMeta& meta);
}

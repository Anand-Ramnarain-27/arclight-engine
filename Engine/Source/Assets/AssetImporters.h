#pragma once

#include "Assets/AssetMeta.h"
#include "Assets/CookedFormats.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>

namespace arc
{
    // Raised whenever an importer's output changes for the same source and settings, so every
    // cooked file it wrote before is rebuilt.
    inline constexpr uint32_t kModelImporterVersion = 4;
    inline constexpr uint32_t kTextureImporterVersion = 2;

    [[nodiscard]] uint32_t importerVersion(AssetType type);

    /// A glTF file's bytes, imported with `settings`: scaled, with levels of detail and with every
    /// drawn image's mip chain built and encoded in the format its material slot calls for, ready
    /// for writeCookedModel(). `path` names the model and
    /// is where references to external buffers and images are resolved from. Null after logging if
    /// the file cannot be imported.
    [[nodiscard]] std::optional<ModelData> importModelAsset(std::span<const std::byte> source, const std::string& path,
                                                            const ModelImportSettings& settings);

    /// An image file's bytes, decoded, reduced to fit settings.maxSize, given its mip chain and
    /// encoded as colour with settings.compression.
    [[nodiscard]] std::optional<CookedTexture> importTextureAsset(std::span<const std::byte> source, const std::string& path,
                                                                  const TextureImportSettings& settings);

    /// Scales the model about its origin by `scale`, which must be positive: root nodes,
    /// instances and the model's bounds. Mesh data and primitive bounds stay in object space.
    void scaleModel(ModelData& data, float scale);

    /// What each of the model's images holds, from the material slots that use it: Color for base
    /// colour and emission, Data for metallic-roughness, Normal and Mask for normal and occlusion
    /// maps. An image used by more than one kind of slot (occlusion packed into the red channel of
    /// a metallic-roughness image) keeps every channel: Color if any slot is colour, else Data.
    [[nodiscard]] std::vector<TextureContent> imageContents(const ModelData& data);
}

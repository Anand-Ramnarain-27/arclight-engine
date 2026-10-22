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
    inline constexpr uint32_t kModelImporterVersion = 1;
    inline constexpr uint32_t kTextureImporterVersion = 1;

    [[nodiscard]] uint32_t importerVersion(AssetType type);

    /// A glTF file's bytes, imported with `settings`: scaled, with levels of detail and with every
    /// base colour image's mip chain built, ready for writeCookedModel(). `path` names the model and
    /// is where references to external buffers and images are resolved from. Null after logging if
    /// the file cannot be imported.
    [[nodiscard]] std::optional<ModelData> importModelAsset(std::span<const std::byte> source, const std::string& path,
                                                            const ModelImportSettings& settings);

    /// An image file's bytes, decoded, reduced to fit settings.maxSize and given its mip chain.
    [[nodiscard]] std::optional<CookedTexture> importTextureAsset(std::span<const std::byte> source, const std::string& path,
                                                                  const TextureImportSettings& settings);

    /// Scales the model about its origin by `scale`, which must be positive: root nodes,
    /// instances and the model's bounds. Mesh data and primitive bounds stay in object space.
    void scaleModel(ModelData& data, float scale);
}

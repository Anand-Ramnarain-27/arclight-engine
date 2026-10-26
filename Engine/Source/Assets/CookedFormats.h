#pragma once

#include "Assets/AssetMeta.h"
#include "Renderer/Image.h"
#include "Renderer/ModelData.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace arc
{
    // Arclight's own binary formats, written by the importers into library:// and read at load
    // time. A cooked model is ready to upload: no glTF parsing, image decoding, mip generation or
    // simplification is left to do. Cooked files are a cache: they can be deleted at any time, and
    // are rebuilt from the source whenever their stamp no longer matches it.

    /// What a cooked file was built from. A cooked file is current while all four match.
    struct CookedStamp
    {
        AssetId id;
        uint32_t importerVersion = 0;
        uint64_t sourceHash = 0;
        uint64_t settingsHash = 0;

        bool operator==(const CookedStamp&) const = default;
    };

    /// The source file's size and modification time when it was last found to match the stamp's
    /// hash. While they still match, the source is assumed unchanged without reading it; when they
    /// differ (an edit, or a git checkout that only touched it), the source is hashed to decide.
    struct SourceFingerprint
    {
        uint64_t size = 0;
        int64_t modifiedTime = 0;

        bool operator==(const SourceFingerprint&) const = default;
    };

    struct CookedHeader
    {
        CookedStamp stamp;
        SourceFingerprint source;
    };

    /// Bytes at the start of every cooked file that readCookedHeader() needs.
    inline constexpr size_t kCookedHeaderSize = 52;

    /// A texture as uploaded: every mip level in its cooked format, level 0 first.
    struct CookedTexture
    {
        std::string name;
        EncodedTexture texture;
    };

    inline constexpr const char* kCookedModelExtension = ".arcmodel";
    inline constexpr const char* kCookedTextureExtension = ".arctex";

    /// Writes the model's images from `data.encodedImages`; `data.images` is not stored.
    [[nodiscard]] std::vector<std::byte> writeCookedModel(const ModelData& data, const CookedHeader& header);

    /// Fails on a wrong magic or format version, a truncated file, or contents a valid model cannot
    /// have: an index or range that points past its array, an image format no importer writes, or
    /// mip levels of the wrong size. The result has `encodedImages` (and `imageColorSpaces`, from
    /// their formats) filled and `images` empty.
    [[nodiscard]] std::optional<ModelData> readCookedModel(std::span<const std::byte> file, CookedHeader* header = nullptr);

    [[nodiscard]] std::vector<std::byte> writeCookedTexture(const CookedTexture& texture, const CookedHeader& header);
    [[nodiscard]] std::optional<CookedTexture> readCookedTexture(std::span<const std::byte> file, CookedHeader* header = nullptr);

    /// Reads only the header, to check whether a cooked file is current without loading it. Needs
    /// at least kCookedHeaderSize bytes.
    [[nodiscard]] std::optional<CookedHeader> readCookedHeader(std::span<const std::byte> file, AssetType type);

    /// Rewrites the source fingerprint in a cooked file's header, leaving the rest as it is.
    void setCookedSource(std::vector<std::byte>& file, const SourceFingerprint& source);
}

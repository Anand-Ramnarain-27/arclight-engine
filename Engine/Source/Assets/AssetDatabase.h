#pragma once

#include "Assets/AssetMeta.h"
#include "Assets/CookedFormats.h"

#include <cstddef>
#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace arc
{
    class VirtualFileSystem;

    enum class AssetState : uint8_t
    {
        /// No cooked copy yet.
        NotImported,
        /// The cooked copy matches the source, the settings and the importer.
        UpToDate,
        /// One of those changed since it was cooked; the next load imports it again.
        Stale,
        /// The last import failed; see AssetRecord::error.
        Failed,
        /// The source file is gone.
        Missing,
    };

    [[nodiscard]] const char* assetStateName(AssetState state);

    struct AssetRecord
    {
        /// Virtual for assets inside a mount; native for files dropped in from elsewhere.
        std::string path;
        AssetMeta meta;
        /// Whether the meta comes from a .meta file. When it does not, the id is pathAssetId() and
        /// settings changes last only for this run.
        bool metaSaved = false;
        AssetState state = AssetState::NotImported;
        uint64_t cookedSize = 0;
        /// Milliseconds the last import in this run took; 0 if it has not been imported this run.
        double importMilliseconds = 0.0;
        std::string error;
    };

    /// Knows every asset under the asset root: its id, its import settings and whether its cooked
    /// copy is current. Imports a source into library:// when it is loaded and its cooked copy is
    /// missing or out of date, and hands out the cooked data.
    ///
    /// Each source file has a .meta file next to it holding its id and settings. A missing one is
    /// written, with a new id, to the first writable layer of the source's mount; where no layer is
    /// writable, the id is derived from the path instead.
    ///
    /// Main thread only.
    class AssetDatabase
    {
    public:
        explicit AssetDatabase(VirtualFileSystem& fileSystem, std::string assetRoot = "assets://",
                               std::string libraryRoot = "library://");

        /// Scans the asset root and rebuilds the records, reading and writing .meta files and
        /// checking each cooked copy. Returns the number of assets.
        size_t refresh();

        [[nodiscard]] const AssetRecord* find(AssetId id) const;
        [[nodiscard]] const AssetRecord* findByPath(std::string_view path) const;

        /// The asset at `path`, registering it if it is an importable file the scan did not see, such
        /// as one outside the asset root. Null if it is not.
        AssetId registerPath(const std::string& path);

        /// Cooks the asset unless its cooked copy is current, or always with `force`. False if the
        /// source cannot be read or imported.
        bool import(AssetId id, bool force = false);
        /// Imports every asset that is not current. Returns how many were imported.
        size_t importAll(bool force = false);

        /// The cooked data, importing first when needed. Null after logging on failure.
        [[nodiscard]] std::optional<ModelData> loadModel(AssetId id);
        [[nodiscard]] std::optional<CookedTexture> loadTexture(AssetId id);

        /// Replaces the asset's import settings and saves them to its .meta file when it has one.
        /// The id and type are kept. The cooked copy becomes stale; the next load re-imports it.
        bool setSettings(AssetId id, const ModelImportSettings& model, const TextureImportSettings& texture);

        /// Sorted by path.
        [[nodiscard]] std::vector<const AssetRecord*> records() const;

        /// Where the asset's cooked copy is kept: library://Models/<id>.arcmodel, for example.
        [[nodiscard]] std::string cookedPath(const AssetRecord& record) const;

        /// .meta files whose source was missing in the last refresh().
        [[nodiscard]] const std::vector<std::string>& orphanedMetas() const { return m_orphanedMetas; }

    private:
        void scan(const std::string& directory, std::vector<std::string>& sources, std::vector<std::string>& metas) const;
        AssetRecord* findRecord(AssetId id);
        AssetId addRecord(AssetRecord record);
        [[nodiscard]] CookedStamp currentStamp(const AssetRecord& record, uint64_t sourceHash) const;
        [[nodiscard]] std::optional<SourceFingerprint> fingerprint(const AssetRecord& record) const;
        [[nodiscard]] std::optional<std::vector<std::byte>> readSource(const AssetRecord& record) const;
        [[nodiscard]] std::optional<CookedHeader> cookedHeader(const AssetRecord& record) const;
        [[nodiscard]] bool matchesExceptSource(const AssetRecord& record, const CookedStamp& stamp) const;
        void updateState(AssetRecord& record);
        /// The cooked file's bytes, cooking it first if it is missing, stale or `force` is set.
        std::optional<std::vector<std::byte>> cooked(AssetRecord& record, bool force);

        VirtualFileSystem& m_fileSystem;
        std::string m_assetRoot;
        std::string m_libraryRoot;
        std::unordered_map<AssetId, AssetRecord> m_records;
        std::map<std::string, AssetId, std::less<>> m_paths;
        std::vector<std::string> m_orphanedMetas;
    };
}

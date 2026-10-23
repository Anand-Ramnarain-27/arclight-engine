#include "Assets/AssetDatabase.h"

#include "Assets/AssetImporters.h"
#include "Core/Hash.h"
#include "Core/Log.h"
#include "Core/Time.h"
#include "Platform/FileSystem.h"
#include "Platform/VirtualFileSystem.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <set>

namespace arc
{
    namespace
    {
        constexpr std::string_view kMetaExtension = ".meta";
    }

    const char* assetStateName(AssetState state)
    {
        switch (state)
        {
            case AssetState::NotImported: return "Not imported";
            case AssetState::UpToDate:    return "Up to date";
            case AssetState::Stale:       return "Out of date";
            case AssetState::Failed:      return "Failed";
            case AssetState::Missing:     return "Missing";
        }
        return "";
    }

    AssetDatabase::AssetDatabase(VirtualFileSystem& fileSystem, std::string assetRoot, std::string libraryRoot)
        : m_fileSystem(fileSystem)
        , m_assetRoot(std::move(assetRoot))
        , m_libraryRoot(std::move(libraryRoot))
    {
    }

    void AssetDatabase::scan(const std::string& directory, std::vector<std::string>& sources, std::vector<std::string>& metas) const
    {
        for (const DirectoryEntry& entry : m_fileSystem.list(directory))
        {
            const std::string path = joinPath(directory, entry.name);
            if (entry.info.type == PathType::Directory)
                scan(path, sources, metas);
            else if (path.ends_with(kMetaExtension))
                metas.push_back(path);
            else if (assetTypeOf(path) != AssetType::Unknown)
                sources.push_back(path);
        }
    }

    size_t AssetDatabase::refresh()
    {
        const Stopwatch timer;

        // Assets registered from outside the root are kept: the scan would not find them again.
        std::unordered_map<AssetId, AssetRecord> kept;
        for (auto& [id, record] : m_records)
            if (!record.path.starts_with(m_assetRoot))
                kept.emplace(id, std::move(record));
        m_records = std::move(kept);
        m_paths.clear();
        for (const auto& [id, record] : m_records)
            m_paths.emplace(record.path, id);

        std::vector<std::string> sources;
        std::vector<std::string> metas;
        scan(m_assetRoot, sources, metas);
        const std::set<std::string, std::less<>> metaFiles(metas.begin(), metas.end());

        size_t written = 0;
        for (const std::string& source : sources)
        {
            AssetRecord record;
            record.path = source;
            const AssetType type = assetTypeOf(source);
            const std::string metaPath = metaPathOf(source);

            if (metaFiles.contains(metaPath))
            {
                const std::optional<std::string> text = m_fileSystem.readText(metaPath);
                const std::optional<AssetMeta> meta = text ? readAssetMeta(*text) : std::nullopt;
                if (meta && meta->type == type)
                {
                    record.meta = *meta;
                    record.metaSaved = true;
                }
                else
                {
                    // Left as it is: overwriting it would lose an id other files may refer to.
                    ARC_LOG_ERROR("'{}' is not a valid .meta file for a {}; using an id from the path until it is fixed",
                                  metaPath, assetTypeName(type));
                }
            }

            if (!record.metaSaved)
            {
                record.meta.type = type;
                record.meta.id = pathAssetId(source);
            }

            // A copied file with its .meta file carries the original's id; the copy gets a new one.
            const bool duplicate = m_records.contains(record.meta.id);
            if (duplicate)
                ARC_LOG_WARN("'{}' has the same id as '{}', probably from a copied .meta file; giving it a new id", source,
                             m_records.at(record.meta.id).path);
            const bool hasMetaFile = metaFiles.contains(metaPath);
            const bool invalidMetaFile = hasMetaFile && !record.metaSaved;
            if (duplicate || !hasMetaFile)
            {
                if (!invalidMetaFile && m_fileSystem.isWritable(metaPath))
                {
                    record.meta.id = Uuid::generate();
                    record.metaSaved = m_fileSystem.writeText(metaPath, writeAssetMeta(record.meta));
                    written += record.metaSaved ? 1 : 0;
                }
                else if (duplicate)
                {
                    record.meta.id = Uuid::generate();
                    record.metaSaved = false;
                }
            }

            addRecord(std::move(record));
        }

        m_orphanedMetas.clear();
        for (const std::string& meta : metas)
        {
            const std::string_view source = std::string_view(meta).substr(0, meta.size() - kMetaExtension.size());
            if (!m_paths.contains(source))
                m_orphanedMetas.push_back(meta);
        }
        for (const std::string& meta : m_orphanedMetas)
            ARC_LOG_WARN("'{}' has no source file next to it; delete it if the asset was removed on purpose", meta);

        size_t pending = 0;
        for (auto& [id, record] : m_records)
        {
            updateState(record);
            pending += record.state != AssetState::UpToDate ? 1 : 0;
        }
        ARC_LOG_INFO("Asset database: {} assets in {}, {} new .meta files, {} to import, in {:.1f} ms", sources.size(),
                     m_assetRoot, written, pending, timer.elapsedMs());
        return sources.size();
    }

    AssetId AssetDatabase::addRecord(AssetRecord record)
    {
        const AssetId id = record.meta.id;
        m_paths.insert_or_assign(record.path, id);
        m_records.insert_or_assign(id, std::move(record));
        return id;
    }

    const AssetRecord* AssetDatabase::find(AssetId id) const
    {
        const auto record = m_records.find(id);
        return record != m_records.end() ? &record->second : nullptr;
    }

    AssetRecord* AssetDatabase::findRecord(AssetId id)
    {
        const auto record = m_records.find(id);
        return record != m_records.end() ? &record->second : nullptr;
    }

    const AssetRecord* AssetDatabase::findByPath(std::string_view path) const
    {
        std::string key(path);
        if (const std::optional<VirtualPath> parsed = parseVirtualPath(path))
            key = parsed->toString();
        const auto found = m_paths.find(key);
        return found != m_paths.end() ? find(found->second) : nullptr;
    }

    AssetId AssetDatabase::registerPath(const std::string& path)
    {
        std::string key = path;
        if (isVirtualPath(path))
        {
            const std::optional<VirtualPath> parsed = parseVirtualPath(path);
            if (!parsed)
                return {};
            key = parsed->toString();
        }
        if (const AssetRecord* existing = findByPath(key))
            return existing->meta.id;

        const AssetType type = assetTypeOf(key);
        const bool exists = isVirtualPath(key) ? m_fileSystem.isFile(key) : fileExists(key);
        if (type == AssetType::Unknown || !exists)
            return {};

        AssetRecord record;
        record.path = key;
        record.meta.type = type;
        record.meta.id = pathAssetId(key);
        if (isVirtualPath(key))
        {
            const std::string metaPath = metaPathOf(key);
            const std::optional<std::string> text = m_fileSystem.readText(metaPath);
            const std::optional<AssetMeta> meta = text ? readAssetMeta(*text) : std::nullopt;
            if (meta && meta->type == type && !m_records.contains(meta->id))
            {
                record.meta = *meta;
                record.metaSaved = true;
            }
            else if (!text && m_fileSystem.isWritable(metaPath))
            {
                // A file added since the last refresh().
                record.meta.id = Uuid::generate();
                record.metaSaved = m_fileSystem.writeText(metaPath, writeAssetMeta(record.meta));
            }
        }
        // Path ids are deterministic, so two spellings of one native file could still collide.
        if (m_records.contains(record.meta.id))
            record.meta.id = Uuid::generate();
        updateState(record);
        return addRecord(std::move(record));
    }

    std::string AssetDatabase::cookedPath(const AssetRecord& record) const
    {
        const bool model = record.meta.type == AssetType::Model;
        return joinPath(m_libraryRoot, std::string(model ? "Models/" : "Textures/") + record.meta.id.toString() +
                                           (model ? kCookedModelExtension : kCookedTextureExtension));
    }

    CookedStamp AssetDatabase::currentStamp(const AssetRecord& record, uint64_t sourceHash) const
    {
        return {.id = record.meta.id,
                .importerVersion = importerVersion(record.meta.type),
                .sourceHash = sourceHash,
                .settingsHash = settingsHash(record.meta)};
    }

    std::optional<SourceFingerprint> AssetDatabase::fingerprint(const AssetRecord& record) const
    {
        if (isVirtualPath(record.path))
        {
            const FileInfo info = m_fileSystem.info(record.path);
            if (info.type != PathType::File)
                return std::nullopt;
            return SourceFingerprint{info.size, info.modifiedTime};
        }
        SDL_PathInfo info;
        if (!SDL_GetPathInfo(record.path.c_str(), &info) || info.type != SDL_PATHTYPE_FILE)
            return std::nullopt;
        return SourceFingerprint{info.size, info.modify_time};
    }

    std::optional<std::vector<std::byte>> AssetDatabase::readSource(const AssetRecord& record) const
    {
        // readFile() rather than the file system for native paths, which no mount covers.
        return isVirtualPath(record.path) ? m_fileSystem.read(record.path) : readFile(record.path);
    }

    std::optional<CookedHeader> AssetDatabase::cookedHeader(const AssetRecord& record) const
    {
        const std::optional<std::vector<std::byte>> prefix = m_fileSystem.readPrefix(cookedPath(record), kCookedHeaderSize);
        return prefix ? readCookedHeader(*prefix, record.meta.type) : std::nullopt;
    }

    bool AssetDatabase::matchesExceptSource(const AssetRecord& record, const CookedStamp& stamp) const
    {
        return stamp.id == record.meta.id && stamp.importerVersion == importerVersion(record.meta.type) &&
               stamp.settingsHash == settingsHash(record.meta);
    }

    void AssetDatabase::updateState(AssetRecord& record)
    {
        const std::optional<SourceFingerprint> source = fingerprint(record);
        if (!source)
        {
            record.state = AssetState::Missing;
            return;
        }
        const std::optional<CookedHeader> header = cookedHeader(record);
        const FileInfo cookedInfo = m_fileSystem.info(cookedPath(record));
        record.cookedSize = cookedInfo.size;
        if (cookedInfo.type != PathType::File)
        {
            record.state = record.error.empty() ? AssetState::NotImported : AssetState::Failed;
            return;
        }
        if (!header || !matchesExceptSource(record, header->stamp))
        {
            record.state = AssetState::Stale;
            return;
        }
        if (header->source == *source)
        {
            record.state = AssetState::UpToDate;
            return;
        }
        // Touched since it was cooked: only its contents decide.
        const std::optional<std::vector<std::byte>> bytes = readSource(record);
        record.state = bytes && fnv1a64(*bytes) == header->stamp.sourceHash ? AssetState::UpToDate : AssetState::Stale;
    }

    std::optional<std::vector<std::byte>> AssetDatabase::cooked(AssetRecord& record, bool force)
    {
        const std::optional<SourceFingerprint> source = fingerprint(record);
        if (!source)
        {
            record.state = AssetState::Missing;
            ARC_LOG_ERROR("Cannot find '{}'", record.path);
            return std::nullopt;
        }
        const std::string target = cookedPath(record);
        const std::optional<CookedHeader> header = force ? std::nullopt : cookedHeader(record);
        const bool settingsMatch = header && matchesExceptSource(record, header->stamp);

        // Unchanged since it was cooked: the source is not even read.
        if (settingsMatch && header->source == *source)
            if (std::optional<std::vector<std::byte>> existing = m_fileSystem.read(target))
            {
                record.state = AssetState::UpToDate;
                record.cookedSize = existing->size();
                return existing;
            }

        const std::optional<std::vector<std::byte>> sourceBytes = readSource(record);
        if (!sourceBytes)
        {
            record.state = AssetState::Missing;
            ARC_LOG_ERROR("Cannot read '{}'", record.path);
            return std::nullopt;
        }
        const CookedStamp stamp = currentStamp(record, fnv1a64(*sourceBytes));

        // Touched but not changed, as after a git checkout: the fingerprint is updated so the next
        // check is quick again.
        if (settingsMatch && header->stamp == stamp)
            if (std::optional<std::vector<std::byte>> existing = m_fileSystem.read(target))
            {
                setCookedSource(*existing, *source);
                m_fileSystem.write(target, *existing);
                record.state = AssetState::UpToDate;
                record.cookedSize = existing->size();
                return existing;
            }

        const Stopwatch timer;
        const CookedHeader newHeader{stamp, *source};
        std::optional<std::vector<std::byte>> bytes;
        if (record.meta.type == AssetType::Model)
        {
            if (const std::optional<ModelData> data = importModelAsset(*sourceBytes, record.path, record.meta.model))
                bytes = writeCookedModel(*data, newHeader);
        }
        else if (record.meta.type == AssetType::Texture)
        {
            if (const std::optional<CookedTexture> texture = importTextureAsset(*sourceBytes, record.path, record.meta.texture))
                bytes = writeCookedTexture(*texture, newHeader);
        }
        if (!bytes)
        {
            record.state = AssetState::Failed;
            record.error = "The importer could not read the file; see the log";
            return std::nullopt;
        }

        record.error.clear();
        record.state = AssetState::UpToDate;
        record.cookedSize = bytes->size();
        record.importMilliseconds = timer.elapsedMs();
        // Still usable this run if the library cannot be written; it is imported again next time.
        if (!m_fileSystem.write(target, *bytes))
            ARC_LOG_WARN("Cannot save the cooked copy of '{}' to {}", record.path, target);
        ARC_LOG_INFO("Imported '{}' in {:.0f} ms ({:.1f} MB cooked)", record.path, record.importMilliseconds,
                     static_cast<double>(bytes->size()) / (1024.0 * 1024.0));
        return bytes;
    }

    bool AssetDatabase::import(AssetId id, bool force)
    {
        AssetRecord* record = findRecord(id);
        return record && cooked(*record, force).has_value();
    }

    size_t AssetDatabase::importAll(bool force)
    {
        size_t imported = 0;
        for (auto& [id, record] : m_records)
            if (force || record.state != AssetState::UpToDate)
                imported += cooked(record, force) ? 1 : 0;
        return imported;
    }

    std::optional<ModelData> AssetDatabase::loadModel(AssetId id)
    {
        AssetRecord* record = findRecord(id);
        if (!record || record->meta.type != AssetType::Model)
            return std::nullopt;
        std::optional<std::vector<std::byte>> bytes = cooked(*record, false);
        if (!bytes)
            return std::nullopt;
        std::optional<ModelData> data = readCookedModel(*bytes);
        if (!data)
        {
            // A damaged or hand-edited library file: rebuilt from the source once.
            ARC_LOG_WARN("The cooked copy of '{}' is damaged; importing it again", record->path);
            bytes = cooked(*record, true);
            data = bytes ? readCookedModel(*bytes) : std::nullopt;
        }
        return data;
    }

    std::optional<CookedTexture> AssetDatabase::loadTexture(AssetId id)
    {
        AssetRecord* record = findRecord(id);
        if (!record || record->meta.type != AssetType::Texture)
            return std::nullopt;
        std::optional<std::vector<std::byte>> bytes = cooked(*record, false);
        if (!bytes)
            return std::nullopt;
        std::optional<CookedTexture> texture = readCookedTexture(*bytes);
        if (!texture)
        {
            ARC_LOG_WARN("The cooked copy of '{}' is damaged; importing it again", record->path);
            bytes = cooked(*record, true);
            texture = bytes ? readCookedTexture(*bytes) : std::nullopt;
        }
        return texture;
    }

    bool AssetDatabase::setSettings(AssetId id, const ModelImportSettings& model, const TextureImportSettings& texture)
    {
        AssetRecord* record = findRecord(id);
        if (!record)
            return false;
        record->meta.model = model;
        record->meta.texture = texture;
        if (record->metaSaved && !m_fileSystem.writeText(metaPathOf(record->path), writeAssetMeta(record->meta)))
            ARC_LOG_WARN("Cannot save the settings of '{}'; they apply until the editor closes", record->path);
        updateState(*record);
        return true;
    }

    std::vector<const AssetRecord*> AssetDatabase::records() const
    {
        std::vector<const AssetRecord*> result;
        for (const auto& [path, id] : m_paths)
            if (const AssetRecord* record = find(id))
                result.push_back(record);
        return result;
    }
}

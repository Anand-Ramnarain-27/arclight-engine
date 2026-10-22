#include "Platform/VirtualFileSystem.h"

#include "Core/Log.h"

#include <algorithm>
#include <atomic>
#include <map>
#include <mutex>

namespace arc
{
    namespace
    {
        std::atomic<VirtualFileSystem*> g_activeFileSystem{nullptr};

        bool isMountNameCharacter(char c, bool allowUppercase)
        {
            return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-' || c == '_' ||
                   (allowUppercase && c >= 'A' && c <= 'Z');
        }

        // The part before the first ':', if it could be a mount name. At least two characters, so
        // "C:\Users" stays a native path.
        std::optional<std::string_view> mountPrefix(std::string_view path, bool allowUppercase)
        {
            const size_t colon = path.find(':');
            if (colon == std::string_view::npos || colon < 2)
                return std::nullopt;
            const std::string_view name = path.substr(0, colon);
            if (!std::all_of(name.begin(), name.end(), [&](char c) { return isMountNameCharacter(c, allowUppercase); }))
                return std::nullopt;
            return name;
        }

        char toLower(char c)
        {
            return c >= 'A' && c <= 'Z' ? static_cast<char>(c - 'A' + 'a') : c;
        }

        bool lessByName(const DirectoryEntry& a, const DirectoryEntry& b)
        {
            const bool aIsDirectory = a.info.type == PathType::Directory;
            const bool bIsDirectory = b.info.type == PathType::Directory;
            if (aIsDirectory != bIsDirectory)
                return aIsDirectory;
            return std::lexicographical_compare(a.name.begin(), a.name.end(), b.name.begin(), b.name.end(),
                                                [](char x, char y) { return toLower(x) < toLower(y); });
        }
    }

    std::string VirtualPath::toString() const
    {
        return mount + "://" + path;
    }

    bool isVirtualPath(std::string_view path)
    {
        return mountPrefix(path, true).has_value();
    }

    std::optional<std::string> normalizeRelativePath(std::string_view path)
    {
        std::vector<std::string_view> parts;
        size_t start = 0;
        while (start <= path.size())
        {
            const size_t end = std::min(path.find_first_of("/\\", start), path.size());
            const std::string_view part = path.substr(start, end - start);
            start = end + 1;

            if (part.empty() || part == ".")
                continue;
            if (part == "..")
            {
                if (parts.empty())
                    return std::nullopt;
                parts.pop_back();
                continue;
            }
            if (part.find(':') != std::string_view::npos || part.find('\0') != std::string_view::npos)
                return std::nullopt;
            parts.push_back(part);
        }

        std::string result;
        for (const std::string_view part : parts)
        {
            if (!result.empty())
                result += '/';
            result += part;
        }
        return result;
    }

    std::optional<VirtualPath> parseVirtualPath(std::string_view path)
    {
        const std::optional<std::string_view> mount = mountPrefix(path, false);
        if (!mount)
            return std::nullopt;
        std::optional<std::string> relative = normalizeRelativePath(path.substr(mount->size() + 1));
        if (!relative)
            return std::nullopt;
        return VirtualPath{std::string(*mount), std::move(*relative)};
    }

    std::string joinPath(std::string_view directory, std::string_view name)
    {
        if (directory.empty())
            return std::string(name);
        const char last = directory.back();
        if (last == '/' || last == '\\' || last == ':')
            return std::string(directory) + std::string(name);
        return std::string(directory) + "/" + std::string(name);
    }

    std::string parentPath(std::string_view path)
    {
        if (const std::optional<VirtualPath> virtualPath = parseVirtualPath(path))
        {
            const size_t slash = virtualPath->path.rfind('/');
            const std::string parent = slash == std::string::npos ? std::string() : virtualPath->path.substr(0, slash);
            return VirtualPath{virtualPath->mount, parent}.toString();
        }
        const size_t slash = path.find_last_of("/\\");
        return slash == std::string_view::npos ? std::string() : std::string(path.substr(0, slash));
    }

    std::string_view fileNameOf(std::string_view path)
    {
        const size_t separator = path.find_last_of("/\\:");
        return separator == std::string_view::npos ? path : path.substr(separator + 1);
    }

    std::string extensionOf(std::string_view path)
    {
        const std::string_view name = fileNameOf(path);
        const size_t dot = name.rfind('.');
        if (dot == std::string_view::npos || dot == 0)
            return {};
        std::string extension(name.substr(dot));
        std::transform(extension.begin(), extension.end(), extension.begin(), toLower);
        return extension;
    }

    MountId VirtualFileSystem::mount(std::string_view name, std::shared_ptr<FileSource> source)
    {
        if (!source || !mountPrefix(std::string(name) + ":", false))
        {
            ARC_LOG_ERROR("Cannot mount '{}': mount names are lowercase letters, digits, '-' and '_', at least two long",
                          name);
            return 0;
        }
        const std::unique_lock lock(m_mutex);
        const MountId id = m_nextId++;
        m_mounts.insert(m_mounts.begin(), Mount{id, std::string(name), std::move(source)});
        return id;
    }

    bool VirtualFileSystem::unmount(MountId id)
    {
        const std::unique_lock lock(m_mutex);
        return std::erase_if(m_mounts, [id](const Mount& mount) { return mount.id == id; }) > 0;
    }

    void VirtualFileSystem::unmountAll()
    {
        const std::unique_lock lock(m_mutex);
        m_mounts.clear();
    }

    std::vector<MountInfo> VirtualFileSystem::mounts() const
    {
        const std::shared_lock lock(m_mutex);
        std::vector<MountInfo> result;
        for (const Mount& mount : m_mounts)
            result.push_back({mount.id, mount.name, mount.source->description(), mount.source->isWritable()});
        // Grouped by name for display; stable, so each name's layers stay in search order.
        std::stable_sort(result.begin(), result.end(), [](const MountInfo& a, const MountInfo& b) { return a.name < b.name; });
        return result;
    }

    bool VirtualFileSystem::isMounted(std::string_view name) const
    {
        const std::shared_lock lock(m_mutex);
        return std::any_of(m_mounts.begin(), m_mounts.end(), [name](const Mount& mount) { return mount.name == name; });
    }

    std::vector<std::shared_ptr<FileSource>> VirtualFileSystem::layers(std::string_view name) const
    {
        const std::shared_lock lock(m_mutex);
        std::vector<std::shared_ptr<FileSource>> result;
        for (const Mount& mount : m_mounts)
            if (mount.name == name)
                result.push_back(mount.source);
        return result;
    }

    FileInfo VirtualFileSystem::info(std::string_view path) const
    {
        const std::optional<VirtualPath> parsed = parseVirtualPath(path);
        if (!parsed)
            return {};
        for (const std::shared_ptr<FileSource>& layer : layers(parsed->mount))
            if (const FileInfo info = layer->info(parsed->path); info.type != PathType::None)
                return info;
        return {};
    }

    std::optional<std::vector<std::byte>> VirtualFileSystem::read(std::string_view path) const
    {
        const std::optional<VirtualPath> parsed = parseVirtualPath(path);
        if (!parsed)
            return std::nullopt;
        // The first layer that has the name decides, so a folder in an upper layer hides a file of
        // the same name below it, as it does in a listing.
        for (const std::shared_ptr<FileSource>& layer : layers(parsed->mount))
        {
            const FileInfo info = layer->info(parsed->path);
            if (info.type == PathType::Directory)
                return std::nullopt;
            if (info.type == PathType::File)
                return layer->read(parsed->path);
        }
        return std::nullopt;
    }

    std::optional<std::string> VirtualFileSystem::readText(std::string_view path) const
    {
        const std::optional<std::vector<std::byte>> bytes = read(path);
        if (!bytes)
            return std::nullopt;
        return std::string(reinterpret_cast<const char*>(bytes->data()), bytes->size());
    }

    std::vector<DirectoryEntry> VirtualFileSystem::list(std::string_view path) const
    {
        std::vector<DirectoryEntry> result;
        const std::optional<VirtualPath> parsed = parseVirtualPath(path);
        if (!parsed)
            return result;

        std::map<std::string, size_t, std::less<>> seen;
        std::vector<DirectoryEntry> entries;
        for (const std::shared_ptr<FileSource>& layer : layers(parsed->mount))
        {
            entries.clear();
            if (!layer->list(parsed->path, entries))
                continue;
            for (DirectoryEntry& entry : entries)
                if (seen.try_emplace(entry.name, result.size()).second)
                    result.push_back(std::move(entry));
        }
        std::sort(result.begin(), result.end(), lessByName);
        return result;
    }

    bool VirtualFileSystem::write(std::string_view path, std::span<const std::byte> data)
    {
        const std::optional<VirtualPath> parsed = parseVirtualPath(path);
        if (!parsed || parsed->path.empty())
        {
            ARC_LOG_ERROR("Cannot write '{}': not a file path inside a mount", path);
            return false;
        }
        for (const std::shared_ptr<FileSource>& layer : layers(parsed->mount))
            if (layer->isWritable())
                return layer->write(parsed->path, data);
        ARC_LOG_ERROR("Cannot write '{}': '{}' has no writable folder mounted", path, parsed->mount);
        return false;
    }

    bool VirtualFileSystem::writeText(std::string_view path, std::string_view text)
    {
        return write(path, std::as_bytes(std::span(text.data(), text.size())));
    }

    bool VirtualFileSystem::createDirectory(std::string_view path)
    {
        const std::optional<VirtualPath> parsed = parseVirtualPath(path);
        if (!parsed)
            return false;
        for (const std::shared_ptr<FileSource>& layer : layers(parsed->mount))
            if (layer->isWritable())
                return layer->createDirectory(parsed->path);
        return false;
    }

    bool VirtualFileSystem::remove(std::string_view path)
    {
        const std::optional<VirtualPath> parsed = parseVirtualPath(path);
        if (!parsed || parsed->path.empty())
            return false;
        for (const std::shared_ptr<FileSource>& layer : layers(parsed->mount))
            if (layer->isWritable() && layer->info(parsed->path).type != PathType::None)
                return layer->remove(parsed->path);
        return false;
    }

    std::string VirtualFileSystem::nativePath(std::string_view path) const
    {
        const std::optional<VirtualPath> parsed = parseVirtualPath(path);
        if (!parsed)
            return {};
        const std::vector<std::shared_ptr<FileSource>> mounted = layers(parsed->mount);
        for (const std::shared_ptr<FileSource>& layer : mounted)
            if (layer->info(parsed->path).type != PathType::None)
                if (std::string native = layer->nativePath(parsed->path); !native.empty())
                    return native;
        for (const std::shared_ptr<FileSource>& layer : mounted)
            if (layer->isWritable())
                if (std::string native = layer->nativePath(parsed->path); !native.empty())
                    return native;
        return {};
    }

    std::string VirtualFileSystem::virtualPath(std::string_view nativePath) const
    {
        const std::shared_lock lock(m_mutex);
        for (const Mount& mount : m_mounts)
        {
            const std::optional<std::string> relative = mount.source->relativePath(nativePath);
            if (!relative)
                continue;
            if (std::optional<std::string> normalized = normalizeRelativePath(*relative))
                return VirtualPath{mount.name, std::move(*normalized)}.toString();
        }
        return {};
    }

    std::string VirtualFileSystem::sourceOf(std::string_view path) const
    {
        const std::optional<VirtualPath> parsed = parseVirtualPath(path);
        if (!parsed)
            return {};
        for (const std::shared_ptr<FileSource>& layer : layers(parsed->mount))
            if (layer->info(parsed->path).type != PathType::None)
                return layer->description();
        return {};
    }

    void setActiveFileSystem(VirtualFileSystem* fileSystem)
    {
        g_activeFileSystem.store(fileSystem);
    }

    VirtualFileSystem* activeFileSystem()
    {
        return g_activeFileSystem.load();
    }
}

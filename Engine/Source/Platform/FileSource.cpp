#include "Platform/FileSource.h"

#include "Core/Log.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cstring>

namespace arc
{
    namespace
    {
        std::string withForwardSlashes(std::string_view path)
        {
            std::string result(path);
            std::replace(result.begin(), result.end(), '\\', '/');
            return result;
        }

        // Windows and the default macOS file systems ignore case, so a path chosen in a dialog may
        // differ in case from the mounted root and still be inside it.
        bool equalPathPrefix(std::string_view path, std::string_view prefix)
        {
            if (path.size() < prefix.size())
                return false;
#if defined(_WIN32) || defined(__APPLE__)
            return std::equal(prefix.begin(), prefix.end(), path.begin(), [](char a, char b) {
                const auto lower = [](char c) { return c >= 'A' && c <= 'Z' ? static_cast<char>(c - 'A' + 'a') : c; };
                return lower(a) == lower(b);
            });
#else
            return path.substr(0, prefix.size()) == prefix;
#endif
        }

        std::string_view parentOf(std::string_view path)
        {
            const size_t slash = path.rfind('/');
            return slash == std::string_view::npos ? std::string_view() : path.substr(0, slash);
        }

        FileInfo toFileInfo(const SDL_PathInfo& info)
        {
            FileInfo result;
            switch (info.type)
            {
                case SDL_PATHTYPE_FILE:      result.type = PathType::File; break;
                case SDL_PATHTYPE_DIRECTORY: result.type = PathType::Directory; break;
                default:                     result.type = PathType::None; break;
            }
            result.size = result.type == PathType::File ? info.size : 0;
            result.modifiedTime = info.modify_time;
            return result;
        }
    }

    DirectorySource::DirectorySource(std::string root, bool writable)
        : m_root(withForwardSlashes(root))
        , m_writable(writable)
    {
        // A drive root ("C:/") or "/" keeps its slash; anything else loses it so joins add exactly one.
        while (m_root.size() > 1 && m_root.back() == '/' && !(m_root.size() == 3 && m_root[1] == ':'))
            m_root.pop_back();
    }

    std::string DirectorySource::nativePath(std::string_view path) const
    {
        if (path.empty())
            return m_root;
        return m_root.back() == '/' ? m_root + std::string(path) : m_root + "/" + std::string(path);
    }

    std::optional<std::string> DirectorySource::relativePath(std::string_view nativePath) const
    {
        const std::string path = withForwardSlashes(nativePath);
        if (path.size() == m_root.size() && equalPathPrefix(path, m_root))
            return std::string();
        const std::string prefix = m_root.back() == '/' ? m_root : m_root + "/";
        if (!equalPathPrefix(path, prefix))
            return std::nullopt;
        return path.substr(prefix.size());
    }

    FileInfo DirectorySource::info(std::string_view path) const
    {
        SDL_PathInfo info;
        if (!SDL_GetPathInfo(nativePath(path).c_str(), &info))
            return {};
        return toFileInfo(info);
    }

    std::optional<std::vector<std::byte>> DirectorySource::read(std::string_view path) const
    {
        const std::string native = nativePath(path);
        size_t size = 0;
        void* data = SDL_LoadFile(native.c_str(), &size);
        if (!data)
            return std::nullopt;
        std::vector<std::byte> bytes(size);
        if (size > 0)
            std::memcpy(bytes.data(), data, size);
        SDL_free(data);
        return bytes;
    }

    bool DirectorySource::list(std::string_view path, std::vector<DirectoryEntry>& entries) const
    {
        const std::string native = nativePath(path);
        std::vector<std::string> names;
        const auto collect = [](void* userData, const char* /*directory*/, const char* name) {
            if (std::strcmp(name, ".") != 0 && std::strcmp(name, "..") != 0)
                static_cast<std::vector<std::string>*>(userData)->emplace_back(name);
            return SDL_ENUM_CONTINUE;
        };
        if (!SDL_EnumerateDirectory(native.c_str(), collect, &names))
            return false;

        const std::string prefix = path.empty() ? std::string() : std::string(path) + "/";
        for (std::string& name : names)
        {
            FileInfo info = this->info(prefix + name);
            // Entries that vanished since the enumeration, or are neither files nor folders.
            if (info.type != PathType::None)
                entries.push_back({std::move(name), info});
        }
        return true;
    }

    bool DirectorySource::write(std::string_view path, std::span<const std::byte> data)
    {
        if (!m_writable || path.empty())
            return false;
        if (!createDirectory(parentOf(path)))
            return false;

        const std::string native = nativePath(path);
        const std::string temporary = native + ".arclight-tmp";
        if (!SDL_SaveFile(temporary.c_str(), data.data(), data.size()))
        {
            ARC_LOG_ERROR("Cannot write '{}': {}", temporary, SDL_GetError());
            return false;
        }
        // Replaces the target in one step on every platform (MoveFileEx with REPLACE_EXISTING on Windows).
        if (!SDL_RenamePath(temporary.c_str(), native.c_str()))
        {
            ARC_LOG_ERROR("Cannot replace '{}': {}", native, SDL_GetError());
            SDL_RemovePath(temporary.c_str());
            return false;
        }
        return true;
    }

    bool DirectorySource::createDirectory(std::string_view path)
    {
        if (!m_writable)
            return info(path).type == PathType::Directory;
        if (!SDL_CreateDirectory(nativePath(path).c_str()))
        {
            ARC_LOG_ERROR("Cannot create folder '{}': {}", nativePath(path), SDL_GetError());
            return false;
        }
        return true;
    }

    bool DirectorySource::remove(std::string_view path)
    {
        // The root is the mount itself; removing it would unmount the folder from under the source.
        if (!m_writable || path.empty())
            return false;
        return SDL_RemovePath(nativePath(path).c_str());
    }

    void MemorySource::addParents(std::string_view path)
    {
        for (std::string_view parent = parentOf(path); !parent.empty(); parent = parentOf(parent))
            m_directories.emplace(parent);
    }

    bool MemorySource::hasChildren(std::string_view directory) const
    {
        const std::string prefix = std::string(directory) + "/";
        const auto startsWithPrefix = [&prefix](const std::string& path) { return path.starts_with(prefix); };
        const auto file = m_files.lower_bound(prefix);
        const auto folder = m_directories.lower_bound(prefix);
        return (file != m_files.end() && startsWithPrefix(file->first)) ||
               (folder != m_directories.end() && startsWithPrefix(*folder));
    }

    void MemorySource::add(std::string_view path, std::span<const std::byte> data)
    {
        const std::lock_guard lock(m_mutex);
        m_files.insert_or_assign(std::string(path), std::vector<std::byte>(data.begin(), data.end()));
        addParents(path);
    }

    void MemorySource::add(std::string_view path, std::string_view text)
    {
        add(path, std::as_bytes(std::span(text.data(), text.size())));
    }

    FileInfo MemorySource::info(std::string_view path) const
    {
        const std::lock_guard lock(m_mutex);
        if (path.empty() || m_directories.contains(path))
            return {.type = PathType::Directory};
        if (const auto file = m_files.find(path); file != m_files.end())
            return {.type = PathType::File, .size = file->second.size()};
        return {};
    }

    std::optional<std::vector<std::byte>> MemorySource::read(std::string_view path) const
    {
        const std::lock_guard lock(m_mutex);
        if (const auto file = m_files.find(path); file != m_files.end())
            return file->second;
        return std::nullopt;
    }

    bool MemorySource::list(std::string_view path, std::vector<DirectoryEntry>& entries) const
    {
        const std::lock_guard lock(m_mutex);
        if (!path.empty() && !m_directories.contains(path))
            return false;
        const std::string prefix = path.empty() ? std::string() : std::string(path) + "/";
        const auto childName = [&prefix](const std::string& candidate) -> std::optional<std::string_view> {
            if (!candidate.starts_with(prefix))
                return std::nullopt;
            const std::string_view rest = std::string_view(candidate).substr(prefix.size());
            if (rest.empty() || rest.find('/') != std::string_view::npos)
                return std::nullopt;
            return rest;
        };
        for (const auto& [file, data] : m_files)
            if (const auto name = childName(file))
                entries.push_back({std::string(*name), {.type = PathType::File, .size = data.size()}});
        for (const std::string& directory : m_directories)
            if (const auto name = childName(directory))
                entries.push_back({std::string(*name), {.type = PathType::Directory}});
        return true;
    }

    bool MemorySource::write(std::string_view path, std::span<const std::byte> data)
    {
        if (!m_writable || path.empty())
            return false;
        {
            const std::lock_guard lock(m_mutex);
            if (m_directories.contains(path))
                return false;
        }
        add(path, data);
        return true;
    }

    bool MemorySource::createDirectory(std::string_view path)
    {
        const std::lock_guard lock(m_mutex);
        if (path.empty() || m_directories.contains(path))
            return true;
        if (!m_writable || m_files.contains(path))
            return false;
        m_directories.emplace(path);
        addParents(path);
        return true;
    }

    bool MemorySource::remove(std::string_view path)
    {
        const std::lock_guard lock(m_mutex);
        if (!m_writable || path.empty())
            return false;
        if (const auto file = m_files.find(path); file != m_files.end())
        {
            m_files.erase(file);
            return true;
        }
        const auto directory = m_directories.find(path);
        if (directory == m_directories.end() || hasChildren(path))
            return false;
        m_directories.erase(directory);
        return true;
    }
}

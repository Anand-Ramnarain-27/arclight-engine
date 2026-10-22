#include "EditorFiles.h"

#include "Core/Log.h"
#include "Platform/VirtualFileSystem.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <optional>

namespace arc
{
    namespace
    {
        // RFC 8089 file URL. Finder rejects a URL with unescaped spaces, so everything outside the
        // unreserved set and the path separators is percent-encoded, byte by byte, which is also
        // correct for UTF-8.
        std::string fileUrl(std::string path)
        {
            std::replace(path.begin(), path.end(), '\\', '/');
            std::string url = path.starts_with('/') ? "file://" : "file:///";
            constexpr char kHex[] = "0123456789ABCDEF";
            for (const char c : path)
            {
                const auto byte = static_cast<unsigned char>(c);
                const bool plain = (byte >= 'a' && byte <= 'z') || (byte >= 'A' && byte <= 'Z') ||
                                   (byte >= '0' && byte <= '9') || c == '-' || c == '_' || c == '.' || c == '~' ||
                                   c == '/' || c == ':';
                if (plain)
                {
                    url += c;
                }
                else
                {
                    url += '%';
                    url += kHex[byte >> 4];
                    url += kHex[byte & 0x0F];
                }
            }
            return url;
        }
    }

    bool isModelFile(std::string_view path)
    {
        const std::string extension = extensionOf(path);
        return extension == ".gltf" || extension == ".glb";
    }

    std::string preferredPath(const VirtualFileSystem& fileSystem, const std::string& path)
    {
        if (isVirtualPath(path))
            return path;
        std::string virtualPath = fileSystem.virtualPath(path);
        return virtualPath.empty() ? path : virtualPath;
    }

    bool showInFileManager(const std::string& nativePath)
    {
        SDL_PathInfo info;
        std::string folder = nativePath;
        if (SDL_GetPathInfo(nativePath.c_str(), &info) && info.type == SDL_PATHTYPE_FILE)
            folder = parentPath(nativePath);
        if (folder.empty() || !SDL_OpenURL(fileUrl(folder).c_str()))
        {
            ARC_LOG_WARN("Cannot open '{}' in the file manager: {}", folder, SDL_GetError());
            return false;
        }
        return true;
    }

    void RecentFiles::load(const VirtualFileSystem& fileSystem)
    {
        m_paths.clear();
        const std::optional<std::string> text = fileSystem.readText(m_file);
        if (!text)
            return;
        std::string_view rest = *text;
        while (!rest.empty() && m_paths.size() < m_capacity)
        {
            const size_t end = rest.find('\n');
            std::string_view line = rest.substr(0, end);
            if (!line.empty() && line.back() == '\r')
                line.remove_suffix(1);
            if (!line.empty() && std::find(m_paths.begin(), m_paths.end(), line) == m_paths.end())
                m_paths.emplace_back(line);
            if (end == std::string_view::npos)
                break;
            rest.remove_prefix(end + 1);
        }
    }

    void RecentFiles::add(VirtualFileSystem& fileSystem, const std::string& path)
    {
        std::erase(m_paths, path);
        m_paths.insert(m_paths.begin(), path);
        if (m_paths.size() > m_capacity)
            m_paths.resize(m_capacity);
        save(fileSystem);
    }

    void RecentFiles::clear(VirtualFileSystem& fileSystem)
    {
        m_paths.clear();
        save(fileSystem);
    }

    void RecentFiles::save(VirtualFileSystem& fileSystem) const
    {
        std::string text;
        for (const std::string& path : m_paths)
            text += path + "\n";
        fileSystem.writeText(m_file, text);
    }
}

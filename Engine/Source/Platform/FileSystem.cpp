#include "Platform/FileSystem.h"

#include "Core/Log.h"
#include "Platform/VirtualFileSystem.h"

#include <SDL3/SDL.h>

#include <cstring>

namespace arc
{
    namespace
    {
        // Logged once per process: every lookup would fail the same way.
        VirtualFileSystem* fileSystemFor(const std::string& path)
        {
            VirtualFileSystem* fileSystem = activeFileSystem();
            static bool warned = false;
            if (!fileSystem && !warned)
            {
                ARC_LOG_ERROR("'{}' is a virtual path, but no file system is active", path);
                warned = true;
            }
            return fileSystem;
        }
    }

    std::optional<std::vector<std::byte>> readFile(const std::string& path)
    {
        if (isVirtualPath(path))
        {
            const VirtualFileSystem* fileSystem = fileSystemFor(path);
            return fileSystem ? fileSystem->read(path) : std::nullopt;
        }

        // SDL's file functions take UTF-8 on every platform; std::filesystem would need wide
        // strings on Windows.
        size_t size = 0;
        void* data = SDL_LoadFile(path.c_str(), &size);
        if (!data)
            return std::nullopt;
        std::vector<std::byte> bytes(size);
        if (size > 0)
            std::memcpy(bytes.data(), data, size);
        SDL_free(data);
        return bytes;
    }

    bool fileExists(const std::string& path)
    {
        if (isVirtualPath(path))
        {
            const VirtualFileSystem* fileSystem = fileSystemFor(path);
            return fileSystem && fileSystem->isFile(path);
        }
        SDL_PathInfo info;
        return SDL_GetPathInfo(path.c_str(), &info) && info.type == SDL_PATHTYPE_FILE;
    }

    std::optional<uint64_t> fileSize(const std::string& path)
    {
        if (isVirtualPath(path))
        {
            const VirtualFileSystem* fileSystem = fileSystemFor(path);
            if (!fileSystem)
                return std::nullopt;
            const FileInfo info = fileSystem->info(path);
            return info.type == PathType::File ? std::optional<uint64_t>(info.size) : std::nullopt;
        }
        SDL_PathInfo info;
        if (!SDL_GetPathInfo(path.c_str(), &info) || info.type != SDL_PATHTYPE_FILE)
            return std::nullopt;
        return info.size;
    }
}

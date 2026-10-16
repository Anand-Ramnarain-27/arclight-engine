#include "Platform/FileSystem.h"

#include <SDL3/SDL.h>

#include <cstring>

namespace arc
{
    std::string findAssetPath(std::string_view relativePath)
    {
        // SDL's file functions take UTF-8 on every platform; std::filesystem would need wide
        // strings on Windows.
        std::string path = "Assets/" + std::string(relativePath);
        if (SDL_GetPathInfo(path.c_str(), nullptr))
            return path;

        const char* basePath = SDL_GetBasePath();
        path = std::string(basePath ? basePath : "") + "Assets/" + std::string(relativePath);
        if (SDL_GetPathInfo(path.c_str(), nullptr))
            return path;

        return {};
    }

    std::optional<std::vector<std::byte>> readFile(const std::string& path)
    {
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
        SDL_PathInfo info;
        return SDL_GetPathInfo(path.c_str(), &info) && info.type == SDL_PATHTYPE_FILE;
    }

    std::optional<uint64_t> fileSize(const std::string& path)
    {
        SDL_PathInfo info;
        if (!SDL_GetPathInfo(path.c_str(), &info) || info.type != SDL_PATHTYPE_FILE)
            return std::nullopt;
        return info.size;
    }
}

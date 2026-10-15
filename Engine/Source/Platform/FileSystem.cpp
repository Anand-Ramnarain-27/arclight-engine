#include "Platform/FileSystem.h"

#include <SDL3/SDL.h>

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
}

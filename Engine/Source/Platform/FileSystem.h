#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace arc
{
    /// The path of a file in the Assets folder, or an empty string if it is not found. Looks in
    /// Assets/ under the working directory first (Game/ when run from Visual Studio or Xcode), then
    /// in Assets/ next to the executable (the release package). UTF-8.
    [[nodiscard]] std::string findAssetPath(std::string_view relativePath);

    /// The whole file, or nothing if it cannot be read. `path` is UTF-8 on every platform.
    [[nodiscard]] std::optional<std::vector<std::byte>> readFile(const std::string& path);

    [[nodiscard]] bool fileExists(const std::string& path);

    /// Bytes, or nothing if `path` is not a file.
    [[nodiscard]] std::optional<uint64_t> fileSize(const std::string& path);
}

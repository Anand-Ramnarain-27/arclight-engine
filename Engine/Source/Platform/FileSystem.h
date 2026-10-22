#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace arc
{
    // Each function takes a virtual path ("assets://Models/a.glb"), resolved through the active
    // VirtualFileSystem, or a native path, such as one from a file dialog or a drop. Both are UTF-8
    // on every platform.

    /// The whole file, or nothing if it cannot be read.
    [[nodiscard]] std::optional<std::vector<std::byte>> readFile(const std::string& path);

    [[nodiscard]] bool fileExists(const std::string& path);

    /// Bytes, or nothing if `path` is not a file.
    [[nodiscard]] std::optional<uint64_t> fileSize(const std::string& path);
}

#pragma once

#include <cstddef>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace arc
{
    class VirtualFileSystem;

    /// The ImGui drag-and-drop payload type for a model path: a NUL-terminated UTF-8 string.
    inline constexpr const char* kModelPathPayload = "ARC_MODEL_PATH";

    [[nodiscard]] bool isModelFile(std::string_view path);

    /// The virtual path of a native path inside a mounted folder, otherwise the native path
    /// unchanged. Paths are stored this way, so they keep working when the project is opened from
    /// another folder or on another machine.
    [[nodiscard]] std::string preferredPath(const VirtualFileSystem& fileSystem, const std::string& path);

    /// Opens a native folder in Explorer or Finder. For a file, opens the folder that contains it.
    bool showInFileManager(const std::string& nativePath);

    /// Most recently used first, without duplicates, kept in a text file with one path per line.
    class RecentFiles
    {
    public:
        RecentFiles(std::string file, size_t capacity) : m_file(std::move(file)), m_capacity(capacity) {}

        void load(const VirtualFileSystem& fileSystem);
        /// Moves `path` to the front and saves the list.
        void add(VirtualFileSystem& fileSystem, const std::string& path);
        void clear(VirtualFileSystem& fileSystem);

        [[nodiscard]] const std::vector<std::string>& paths() const { return m_paths; }

    private:
        void save(VirtualFileSystem& fileSystem) const;

        std::string m_file;
        size_t m_capacity;
        std::vector<std::string> m_paths;
    };
}

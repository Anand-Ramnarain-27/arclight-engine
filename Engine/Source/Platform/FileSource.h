#pragma once

#include <cstddef>
#include <cstdint>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <set>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace arc
{
    enum class PathType : uint8_t
    {
        None,
        File,
        Directory,
    };

    struct FileInfo
    {
        PathType type = PathType::None;
        /// Bytes; 0 for directories.
        uint64_t size = 0;
        /// Nanoseconds since the Unix epoch, or 0 when the source does not know.
        int64_t modifiedTime = 0;
    };

    struct DirectoryEntry
    {
        std::string name;
        FileInfo info;
    };

    /// A tree of files mounted into the VirtualFileSystem. Paths given to a source are already
    /// normalised: relative to its root, '/'-separated, with no '.' or '..' and no leading or
    /// trailing slash; "" is the root itself.
    ///
    /// Sources are called from any thread, so every member must be safe to call concurrently.
    class FileSource
    {
    public:
        virtual ~FileSource() = default;

        /// What the source reads from, for the editor and logs: a native folder, or "memory".
        [[nodiscard]] virtual std::string description() const = 0;
        [[nodiscard]] virtual bool isWritable() const = 0;

        [[nodiscard]] virtual FileInfo info(std::string_view path) const = 0;
        [[nodiscard]] virtual std::optional<std::vector<std::byte>> read(std::string_view path) const = 0;
        /// Fills `entries` with the directory's children, in no particular order.
        [[nodiscard]] virtual bool list(std::string_view path, std::vector<DirectoryEntry>& entries) const = 0;

        /// Creates missing parent directories. Replaces an existing file.
        virtual bool write(std::string_view path, std::span<const std::byte> data) = 0;
        /// Also creates missing parents; succeeds if the directory already exists.
        virtual bool createDirectory(std::string_view path) = 0;
        /// A file, or an empty directory.
        virtual bool remove(std::string_view path) = 0;

        /// The native path of `path`, whether or not it exists, or empty for sources that are not
        /// native folders.
        [[nodiscard]] virtual std::string nativePath(std::string_view /*path*/) const { return {}; }

        /// The source-relative path of a native path inside this source, if it is inside.
        [[nodiscard]] virtual std::optional<std::string> relativePath(std::string_view /*nativePath*/) const
        {
            return std::nullopt;
        }
    };

    /// A native folder. Writes go to a temporary file that is then renamed over the target, so a
    /// crash or a full disk never leaves a half-written file behind.
    class DirectorySource final : public FileSource
    {
    public:
        /// `root` is a native, UTF-8 path; it does not need to exist yet.
        DirectorySource(std::string root, bool writable);

        [[nodiscard]] std::string description() const override { return m_root; }
        [[nodiscard]] bool isWritable() const override { return m_writable; }
        [[nodiscard]] const std::string& root() const { return m_root; }

        [[nodiscard]] FileInfo info(std::string_view path) const override;
        [[nodiscard]] std::optional<std::vector<std::byte>> read(std::string_view path) const override;
        [[nodiscard]] bool list(std::string_view path, std::vector<DirectoryEntry>& entries) const override;
        bool write(std::string_view path, std::span<const std::byte> data) override;
        bool createDirectory(std::string_view path) override;
        bool remove(std::string_view path) override;
        [[nodiscard]] std::string nativePath(std::string_view path) const override;
        [[nodiscard]] std::optional<std::string> relativePath(std::string_view nativePath) const override;

    private:
        std::string m_root;
        bool m_writable;
    };

    /// Files held in memory: for tests, and for data built into the executable.
    class MemorySource final : public FileSource
    {
    public:
        explicit MemorySource(bool writable = true) : m_writable(writable) {}

        /// Adds or replaces a file regardless of isWritable(), for filling a read-only source.
        void add(std::string_view path, std::span<const std::byte> data);
        void add(std::string_view path, std::string_view text);

        [[nodiscard]] std::string description() const override { return "memory"; }
        [[nodiscard]] bool isWritable() const override { return m_writable; }

        [[nodiscard]] FileInfo info(std::string_view path) const override;
        [[nodiscard]] std::optional<std::vector<std::byte>> read(std::string_view path) const override;
        [[nodiscard]] bool list(std::string_view path, std::vector<DirectoryEntry>& entries) const override;
        bool write(std::string_view path, std::span<const std::byte> data) override;
        bool createDirectory(std::string_view path) override;
        bool remove(std::string_view path) override;

    private:
        void addParents(std::string_view path);
        [[nodiscard]] bool hasChildren(std::string_view directory) const;

        mutable std::mutex m_mutex;
        std::map<std::string, std::vector<std::byte>, std::less<>> m_files;
        std::set<std::string, std::less<>> m_directories;
        bool m_writable;
    };
}

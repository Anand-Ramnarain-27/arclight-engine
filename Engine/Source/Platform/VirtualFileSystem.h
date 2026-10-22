#pragma once

#include "Platform/FileSource.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <shared_mutex>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace arc
{
    /// A path inside the virtual file system, written "mount://folder/file.ext": the mount name,
    /// then a path relative to the mount. Mount names are lowercase letters, digits, '-' and '_',
    /// at least two characters long, so a Windows drive ("C:\") is never mistaken for one.
    struct VirtualPath
    {
        std::string mount;
        /// Normalised: '/'-separated, no '.' or '..', no leading or trailing slash; "" is the root.
        std::string path;

        /// The canonical form, "mount://path".
        [[nodiscard]] std::string toString() const;
    };

    /// True for anything that starts like a virtual path, valid or not.
    [[nodiscard]] bool isVirtualPath(std::string_view path);

    /// Accepts "mount://a/b", "mount:/a/b" and "mount:a/b", and '\' as a separator. Fails for a
    /// native path, an invalid mount name, or a path whose '..' would leave the mount.
    [[nodiscard]] std::optional<VirtualPath> parseVirtualPath(std::string_view path);

    /// Resolves '.' and '..' and repeated separators. Fails if '..' would go above the start, or if
    /// a part contains ':', which could name a drive or a stream on Windows.
    [[nodiscard]] std::optional<std::string> normalizeRelativePath(std::string_view path);

    /// Path helpers that work on virtual and native paths alike.
    [[nodiscard]] std::string joinPath(std::string_view directory, std::string_view name);
    /// Everything before the last separator; "mount://" for a file at the root of a mount.
    [[nodiscard]] std::string parentPath(std::string_view path);
    [[nodiscard]] std::string_view fileNameOf(std::string_view path);
    /// Lowercase, with the dot: ".glb". Empty when there is none.
    [[nodiscard]] std::string extensionOf(std::string_view path);

    using MountId = uint32_t;

    struct MountInfo
    {
        MountId id = 0;
        std::string name;
        std::string description;
        bool writable = false;
    };

    /// Folders and other file sources mounted under names, so code refers to "assets://Models/a.glb"
    /// instead of a path that depends on where the program was started from.
    ///
    /// Several sources may share a mount name. They form layers: the most recently mounted one is
    /// searched first, so a folder mounted later overrides files of the same name in earlier ones,
    /// and a directory listing merges all of them. Writes go to the first writable layer.
    ///
    /// Safe to use from any thread. The mount table is locked only while it is read or changed;
    /// file access happens outside the lock.
    class VirtualFileSystem
    {
    public:
        VirtualFileSystem() = default;
        VirtualFileSystem(const VirtualFileSystem&) = delete;
        VirtualFileSystem& operator=(const VirtualFileSystem&) = delete;

        /// Returns 0 if the name is not a valid mount name.
        MountId mount(std::string_view name, std::shared_ptr<FileSource> source);
        bool unmount(MountId id);
        void unmountAll();
        /// In search order: for each name, its layers from the most recently mounted.
        [[nodiscard]] std::vector<MountInfo> mounts() const;
        [[nodiscard]] bool isMounted(std::string_view name) const;

        [[nodiscard]] FileInfo info(std::string_view path) const;
        [[nodiscard]] bool exists(std::string_view path) const { return info(path).type != PathType::None; }
        [[nodiscard]] bool isFile(std::string_view path) const { return info(path).type == PathType::File; }
        [[nodiscard]] bool isDirectory(std::string_view path) const { return info(path).type == PathType::Directory; }

        [[nodiscard]] std::optional<std::vector<std::byte>> read(std::string_view path) const;
        [[nodiscard]] std::optional<std::string> readText(std::string_view path) const;

        /// The children of a directory in every layer, each name once (the first layer that has it
        /// wins), folders first, then by name.
        [[nodiscard]] std::vector<DirectoryEntry> list(std::string_view path) const;

        bool write(std::string_view path, std::span<const std::byte> data);
        bool writeText(std::string_view path, std::string_view text);
        bool createDirectory(std::string_view path);
        /// Removes a file or an empty directory from the first writable layer that has it.
        bool remove(std::string_view path);

        /// The native path of the layer that has `path`, or, if none has it, of the first writable
        /// native layer. Empty for memory layers and invalid paths.
        [[nodiscard]] std::string nativePath(std::string_view path) const;

        /// The virtual path of a native file or folder inside a mounted folder, or empty. When
        /// several layers contain it, the first in search order is used.
        [[nodiscard]] std::string virtualPath(std::string_view nativePath) const;

        /// The description of the layer `path` is read from, or empty if no layer has it.
        [[nodiscard]] std::string sourceOf(std::string_view path) const;

    private:
        struct Mount
        {
            MountId id;
            std::string name;
            std::shared_ptr<FileSource> source;
        };

        /// A copy of the layers mounted under `name`, so they can be used without the lock.
        [[nodiscard]] std::vector<std::shared_ptr<FileSource>> layers(std::string_view name) const;

        mutable std::shared_mutex m_mutex;
        // Newest first, which is the search order.
        std::vector<Mount> m_mounts;
        MountId m_nextId = 1;
    };

    /// The file system that readFile() and the other functions in FileSystem.h use for virtual
    /// paths. The Application installs its own for as long as it exists.
    void setActiveFileSystem(VirtualFileSystem* fileSystem);
    [[nodiscard]] VirtualFileSystem* activeFileSystem();
}

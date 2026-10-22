#pragma once

#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>

struct SDL_Window;

namespace arc
{
    struct FileDialogQueue;

    struct FileFilter
    {
        std::string name;
        /// Extensions without dots, separated by ';' ("gltf;glb"), or "*" for any file.
        std::string extensions;
    };

    enum class FileDialogType : uint8_t
    {
        OpenFile,
        SaveFile,
        OpenFolder,
    };

    struct FileDialogDesc
    {
        FileDialogType type = FileDialogType::OpenFile;
        std::string title;
        /// Ignored for folders. Not every platform shows them.
        std::vector<FileFilter> filters;
        /// A native folder, or a file to suggest; may be empty.
        std::string defaultLocation;
        bool allowMany = false;
    };

    struct FileDialogResult
    {
        enum class Status : uint8_t
        {
            Chosen,
            Cancelled,
            Failed,
        };

        Status status = Status::Cancelled;
        /// Native UTF-8 paths, in the order the dialog gave them.
        std::vector<std::string> paths;
        /// The filter the user had selected, or -1 when the platform does not say.
        int filter = -1;
        /// Why the dialog could not be shown, when status is Failed.
        std::string error;
    };

    /// The operating system's file dialogs, through SDL. A dialog does not block: SDL calls back
    /// when it closes, on whichever thread the platform uses. The result is queued and the
    /// callback passed to open() runs on the main thread, in dispatch().
    class FileDialogs
    {
    public:
        using Callback = std::function<void(const FileDialogResult&)>;

        FileDialogs();
        ~FileDialogs();
        FileDialogs(const FileDialogs&) = delete;
        FileDialogs& operator=(const FileDialogs&) = delete;

        /// Main thread only. `parent` may be null; the dialog is modal for it where supported.
        void open(SDL_Window* parent, FileDialogDesc desc, Callback onClose);

        /// Runs the callbacks of dialogs that have closed. Main thread only.
        void dispatch();

        [[nodiscard]] bool isOpen() const { return !m_callbacks.empty(); }

    private:
        // Shared with the requests SDL holds, so a dialog that closes after this object is gone
        // still has somewhere to put its result.
        std::shared_ptr<FileDialogQueue> m_queue;
        std::map<uint32_t, Callback> m_callbacks;
        uint32_t m_nextId = 1;
    };
}

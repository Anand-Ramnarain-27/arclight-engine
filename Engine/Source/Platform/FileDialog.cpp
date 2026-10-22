#include "Platform/FileDialog.h"

#include "Core/Assert.h"

#include <SDL3/SDL.h>

#include <mutex>
#include <utility>

namespace arc
{
    struct FileDialogQueue
    {
        std::mutex mutex;
        std::vector<std::pair<uint32_t, FileDialogResult>> closed;
    };

    namespace
    {
        // Owns everything SDL may still read while the dialog is open: the filter array must stay
        // valid until the callback, and SDL only copies the other properties.
        struct Request
        {
            std::shared_ptr<FileDialogQueue> queue;
            uint32_t id = 0;
            std::vector<FileFilter> filters;
            std::vector<SDL_DialogFileFilter> sdlFilters;
        };

        void SDLCALL onDialogClosed(void* userData, const char* const* files, int filter)
        {
            // Deleted here: SDL calls back exactly once per dialog, possibly before the Show call returns.
            std::unique_ptr<Request> request(static_cast<Request*>(userData));

            FileDialogResult result;
            result.filter = filter;
            if (!files)
            {
                result.status = FileDialogResult::Status::Failed;
                result.error = SDL_GetError();
            }
            else
            {
                for (const char* const* file = files; *file; ++file)
                    result.paths.emplace_back(*file);
                result.status = result.paths.empty() ? FileDialogResult::Status::Cancelled
                                                     : FileDialogResult::Status::Chosen;
            }

            const std::lock_guard lock(request->queue->mutex);
            request->queue->closed.emplace_back(request->id, std::move(result));
        }

        SDL_FileDialogType toSdl(FileDialogType type)
        {
            switch (type)
            {
                case FileDialogType::OpenFile:   return SDL_FILEDIALOG_OPENFILE;
                case FileDialogType::SaveFile:   return SDL_FILEDIALOG_SAVEFILE;
                case FileDialogType::OpenFolder: return SDL_FILEDIALOG_OPENFOLDER;
            }
            return SDL_FILEDIALOG_OPENFILE;
        }
    }

    FileDialogs::FileDialogs() : m_queue(std::make_shared<FileDialogQueue>()) {}

    // Dialogs still open keep the queue alive through their requests; their results are dropped.
    FileDialogs::~FileDialogs() = default;

    void FileDialogs::open(SDL_Window* parent, FileDialogDesc desc, Callback onClose)
    {
        const uint32_t id = m_nextId++;
        m_callbacks.emplace(id, std::move(onClose));

        auto request = std::make_unique<Request>();
        request->queue = m_queue;
        request->id = id;
        if (desc.type != FileDialogType::OpenFolder)
            request->filters = std::move(desc.filters);
        for (const FileFilter& filter : request->filters)
            request->sdlFilters.push_back({filter.name.c_str(), filter.extensions.c_str()});

        const SDL_PropertiesID properties = SDL_CreateProperties();
        if (!request->sdlFilters.empty())
        {
            SDL_SetPointerProperty(properties, SDL_PROP_FILE_DIALOG_FILTERS_POINTER, request->sdlFilters.data());
            SDL_SetNumberProperty(properties, SDL_PROP_FILE_DIALOG_NFILTERS_NUMBER,
                                  static_cast<Sint64>(request->sdlFilters.size()));
        }
        if (parent)
            SDL_SetPointerProperty(properties, SDL_PROP_FILE_DIALOG_WINDOW_POINTER, parent);
        if (!desc.defaultLocation.empty())
            SDL_SetStringProperty(properties, SDL_PROP_FILE_DIALOG_LOCATION_STRING, desc.defaultLocation.c_str());
        if (!desc.title.empty())
            SDL_SetStringProperty(properties, SDL_PROP_FILE_DIALOG_TITLE_STRING, desc.title.c_str());
        SDL_SetBooleanProperty(properties, SDL_PROP_FILE_DIALOG_MANY_BOOLEAN, desc.allowMany);

        // Ownership passes to the callback, which SDL always calls, also when the dialog fails.
        SDL_ShowFileDialogWithProperties(toSdl(desc.type), onDialogClosed, request.release(), properties);
        SDL_DestroyProperties(properties);
    }

    void FileDialogs::dispatch()
    {
        std::vector<std::pair<uint32_t, FileDialogResult>> closed;
        {
            const std::lock_guard lock(m_queue->mutex);
            closed.swap(m_queue->closed);
        }
        for (auto& [id, result] : closed)
        {
            const auto callback = m_callbacks.find(id);
            ARC_ASSERT(callback != m_callbacks.end());
            if (callback == m_callbacks.end())
                continue;
            // Removed before the call, so a callback can open another dialog.
            Callback onClose = std::move(callback->second);
            m_callbacks.erase(callback);
            if (onClose)
                onClose(result);
        }
    }
}

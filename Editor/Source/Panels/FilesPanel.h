#pragma once

#include "Panels/Panel.h"
#include "Platform/FileSource.h"

#include <functional>
#include <map>
#include <string>
#include <vector>

namespace arc
{
    class VirtualFileSystem;

    /// Browses the virtual file system: every mount as a tree, with the layer each file comes
    /// from. Double-click a model to import it, or drag it into the Scene view; right-click for
    /// more. Listings are cached and refreshed every few seconds, or with Refresh.
    class FilesPanel final : public Panel
    {
    public:
        struct Actions
        {
            std::function<void(const std::string& path)> importModel;
            std::function<void(const std::string& folder)> loadSkybox;
        };

        FilesPanel(VirtualFileSystem& fileSystem, Actions actions)
            : m_fileSystem(fileSystem)
            , m_actions(std::move(actions))
        {
        }

        [[nodiscard]] const char* title() const override { return "Files"; }
        void draw(bool* open) override;

    private:
        struct Entry
        {
            DirectoryEntry entry;
            std::string path;
            /// Where the entry is read from: the description of the first layer that has it.
            std::string layer;
        };

        [[nodiscard]] const std::vector<Entry>& entries(const std::string& directory);
        [[nodiscard]] bool holdsSkybox(const std::string& directory);
        void drawDirectory(const std::string& directory);
        void drawContextMenu(const Entry& entry);
        void drawMounts();

        VirtualFileSystem& m_fileSystem;
        Actions m_actions;
        std::map<std::string, std::vector<Entry>, std::less<>> m_cache;
        double m_cacheTime = -1.0;
        std::string m_selected;
    };
}

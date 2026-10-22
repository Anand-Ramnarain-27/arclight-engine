#pragma once

#include "Assets/AssetMeta.h"
#include "Panels/Panel.h"
#include "Platform/FileSource.h"

#include <functional>
#include <map>
#include <string>
#include <vector>

namespace arc
{
    class AssetModule;
    class VirtualFileSystem;

    /// Browses the virtual file system: every mount as a tree, with the layer each file comes
    /// from. Double-click a model or prefab to put it in the scene (or drag it into the Scene view),
    /// or a scene to open it; right-click for more. Listings are cached and refreshed every few seconds; Refresh also rescans the assets.
    ///
    /// Selecting an asset shows its id, whether its cooked copy is current, and its import
    /// settings, which Apply saves to its .meta file and imports with. .meta files are not listed.
    class FilesPanel final : public Panel
    {
    public:
        struct Actions
        {
            /// A model or prefab to put in the scene, or a scene to open.
            std::function<void(const std::string& path)> open;
            std::function<void(const std::string& folder)> loadSkybox;
        };

        FilesPanel(VirtualFileSystem& fileSystem, AssetModule& assets, Actions actions)
            : m_fileSystem(fileSystem)
            , m_assets(assets)
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
        void drawLoadedResources();
        void drawAssetDetails(AssetId asset);

        VirtualFileSystem& m_fileSystem;
        AssetModule& m_assets;
        Actions m_actions;
        // The settings being edited in the details pane, for m_editedAsset.
        AssetId m_editedAsset;
        ModelImportSettings m_modelSettings;
        TextureImportSettings m_textureSettings;
        std::map<std::string, std::vector<Entry>, std::less<>> m_cache;
        double m_cacheTime = -1.0;
        std::string m_selected;
    };
}

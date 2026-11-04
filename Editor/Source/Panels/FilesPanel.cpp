#include "Panels/FilesPanel.h"

#include "Assets/AssetModule.h"
#include "EditorFiles.h"
#include "Platform/VirtualFileSystem.h"

#include <imgui.h>

#include <algorithm>
#include <array>
#include <format>
#include <set>
#include <string_view>

namespace arc
{
    namespace
    {
        constexpr double kRefreshSeconds = 2.0;
        constexpr std::array<std::string_view, 6> kSkyboxFaces = {"px", "nx", "py", "ny", "pz", "nz"};

        std::string formatSize(uint64_t bytes)
        {
            if (bytes < 1024)
                return std::format("{} B", bytes);
            const double kilobytes = static_cast<double>(bytes) / 1024.0;
            if (kilobytes < 1024.0)
                return std::format("{:.1f} KB", kilobytes);
            return std::format("{:.1f} MB", kilobytes / 1024.0);
        }

        void compressionCombo(const char* label, TextureCompression& compression)
        {
            int choice = static_cast<int>(compression);
            const char* names[] = {textureCompressionName(TextureCompression::None),
                                   textureCompressionName(TextureCompression::Quality),
                                   textureCompressionName(TextureCompression::Small)};
            if (ImGui::Combo(label, &choice, names, static_cast<int>(std::size(names))))
                compression = static_cast<TextureCompression>(choice);
            ImGui::SetItemTooltip("None: RGBA8, 4 bytes a pixel. Quality: BC7, 1 byte a pixel. Small: BC1 (BC3 with "
                                  "alpha), half a byte. Normal maps are BC5 and occlusion BC4 when compressed.");
        }

        std::string_view stem(std::string_view name)
        {
            const size_t dot = name.rfind('.');
            return dot == std::string_view::npos ? name : name.substr(0, dot);
        }
    }

    const std::vector<FilesPanel::Entry>& FilesPanel::entries(const std::string& directory)
    {
        if (const auto cached = m_cache.find(directory); cached != m_cache.end())
            return cached->second;

        std::vector<Entry> result;
        for (DirectoryEntry& entry : m_fileSystem.list(directory))
        {
            // A .meta file belongs to its asset and is edited through the details pane.
            if (entry.info.type == PathType::File && entry.name.ends_with(".meta"))
                continue;
            std::string path = joinPath(directory, entry.name);
            std::string layer = m_fileSystem.sourceOf(path);
            result.push_back({std::move(entry), std::move(path), std::move(layer)});
        }
        return m_cache.emplace(directory, std::move(result)).first->second;
    }

    bool FilesPanel::holdsSkybox(const std::string& directory)
    {
        std::set<std::string_view> faces;
        for (const Entry& entry : entries(directory))
            if (entry.entry.info.type == PathType::File)
                faces.insert(stem(entry.entry.name));
        return std::all_of(kSkyboxFaces.begin(), kSkyboxFaces.end(), [&](std::string_view face) { return faces.contains(face); });
    }

    void FilesPanel::draw(bool* open)
    {
        if (!ImGui::Begin(title(), open))
        {
            ImGui::End();
            return;
        }

        AssetDatabase& database = m_assets.database();
        const double now = ImGui::GetTime();
        const bool refresh = ImGui::Button("Refresh");
        if (refresh)
            database.refresh();
        if (refresh || now - m_cacheTime > kRefreshSeconds)
        {
            m_cache.clear();
            m_cacheTime = now;
        }
        ImGui::SetItemTooltip("Lists the folders again and rescans assets:// for new and changed assets");
        ImGui::SameLine();
        if (ImGui::Button("Import Changed"))
            database.importAll();
        ImGui::SetItemTooltip("Cooks every asset whose cooked copy is missing or out of date");
        ImGui::SameLine();
        if (ImGui::Button("Loaded"))
            ImGui::OpenPopup("Loaded Resources");
        drawLoadedResources();
        ImGui::SameLine();
        if (ImGui::Button("Mounts"))
            ImGui::OpenPopup("Mounts");
        drawMounts();
        ImGui::SameLine();
        ImGui::TextDisabled("%s", m_selected.empty() ? "Double-click a model, prefab or scene to open it" : m_selected.c_str());

        const AssetRecord* selectedAsset = m_selected.empty() ? nullptr : database.findByPath(m_selected);
        const float detailsHeight = selectedAsset ? ImGui::GetTextLineHeightWithSpacing() * 11.0f : 0.0f;
        constexpr ImGuiTableFlags kTableFlags = ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV |
                                                ImGuiTableFlags_Resizable | ImGuiTableFlags_ScrollY;
        if (ImGui::BeginTable("##Files", 3, kTableFlags, {0.0f, -detailsHeight}))
        {
            ImGui::TableSetupScrollFreeze(0, 1);
            ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthStretch, 3.0f);
            ImGui::TableSetupColumn("Size", ImGuiTableColumnFlags_WidthFixed, ImGui::GetFontSize() * 5.0f);
            ImGui::TableSetupColumn("From", ImGuiTableColumnFlags_WidthStretch, 2.0f);
            ImGui::TableHeadersRow();

            std::vector<std::string> names;
            for (const MountInfo& mount : m_fileSystem.mounts())
                if (std::find(names.begin(), names.end(), mount.name) == names.end())
                    names.push_back(mount.name);

            for (const std::string& name : names)
            {
                const std::string root = name + "://";
                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                const ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_SpanFullWidth |
                                                 (name == "assets" ? ImGuiTreeNodeFlags_DefaultOpen : 0);
                if (ImGui::TreeNodeEx(root.c_str(), flags))
                {
                    drawDirectory(root);
                    ImGui::TreePop();
                }
            }
            ImGui::EndTable();
        }
        if (selectedAsset)
            drawAssetDetails(selectedAsset->meta.id);
        ImGui::End();
    }

    void FilesPanel::drawDirectory(const std::string& directory)
    {
        // Copied: drawing a child may refresh the cache, which would invalidate a reference.
        const std::vector<Entry> children = entries(directory);
        for (const Entry& child : children)
        {
            const bool isDirectory = child.entry.info.type == PathType::Directory;
            ImGui::TableNextRow();
            ImGui::TableNextColumn();

            ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_SpanFullWidth | ImGuiTreeNodeFlags_OpenOnArrow |
                                       ImGuiTreeNodeFlags_OpenOnDoubleClick;
            if (!isDirectory)
                flags |= ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen;
            if (child.path == m_selected)
                flags |= ImGuiTreeNodeFlags_Selected;
            const bool opened = ImGui::TreeNodeEx(child.path.c_str(), flags, "%s", child.entry.name.c_str());

            if (ImGui::IsItemClicked() && !ImGui::IsItemToggledOpen())
                m_selected = child.path;
            const bool placeable = isModelFile(child.path) || isPrefabFile(child.path);
            if (!isDirectory && (placeable || isSceneFile(child.path)))
                if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left) && m_actions.open)
                    m_actions.open(child.path);
            if (!isDirectory && placeable && ImGui::BeginDragDropSource())
            {
                ImGui::SetDragDropPayload(kAssetPathPayload, child.path.c_str(), child.path.size() + 1);
                ImGui::TextUnformatted(child.path.c_str());
                ImGui::EndDragDropSource();
            }
            drawContextMenu(child);

            ImGui::TableNextColumn();
            if (!isDirectory)
                ImGui::TextDisabled("%s", formatSize(child.entry.info.size).c_str());
            ImGui::TableNextColumn();
            ImGui::TextDisabled("%s", child.layer.c_str());
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("%s", child.layer.c_str());

            if (isDirectory && opened)
            {
                drawDirectory(child.path);
                ImGui::TreePop();
            }
        }
    }

    void FilesPanel::drawContextMenu(const Entry& entry)
    {
        if (!ImGui::BeginPopupContextItem())
            return;
        m_selected = entry.path;
        const bool isDirectory = entry.entry.info.type == PathType::Directory;
        if (!isDirectory && m_actions.open)
        {
            if (isModelFile(entry.path) && ImGui::MenuItem("Import into Scene"))
                m_actions.open(entry.path);
            if (isPrefabFile(entry.path) && ImGui::MenuItem("Place in Scene"))
                m_actions.open(entry.path);
            if (isSceneFile(entry.path) && ImGui::MenuItem("Open Scene"))
                m_actions.open(entry.path);
        }
        if (!isDirectory && isHdrFile(entry.path) && m_actions.loadSkybox && ImGui::MenuItem("Use as Sky"))
            m_actions.loadSkybox(entry.path);
        if (!isDirectory && assetTypeOf(entry.path) != AssetType::Unknown)
            if (ImGui::MenuItem("Reimport"))
                if (const AssetId asset = m_assets.database().registerPath(entry.path))
                    m_assets.database().import(asset, true);
        if (isDirectory)
        {
            const bool skybox = holdsSkybox(entry.path);
            if (ImGui::MenuItem("Use as Sky", nullptr, false, skybox) && m_actions.loadSkybox)
                m_actions.loadSkybox(entry.path);
            if (!skybox)
                ImGui::SetItemTooltip("Needs six images named px, nx, py, ny, pz and nz");
        }
        ImGui::Separator();
        if (ImGui::MenuItem("Copy Path"))
            ImGui::SetClipboardText(entry.path.c_str());
        const std::string native = m_fileSystem.nativePath(entry.path);
        if (ImGui::MenuItem("Show in File Manager", nullptr, false, !native.empty()))
            showInFileManager(native);
        ImGui::EndPopup();
    }

    void FilesPanel::drawMounts()
    {
        if (!ImGui::BeginPopup("Mounts"))
            return;
        ImGui::TextDisabled("Layers of the same name are searched top to bottom.");
        for (const MountInfo& mount : m_fileSystem.mounts())
            ImGui::BulletText("%s://  %s%s", mount.name.c_str(), mount.description.c_str(),
                              mount.writable ? "  (writable)" : "  (read-only)");
        ImGui::EndPopup();
    }

    void FilesPanel::drawLoadedResources()
    {
        if (!ImGui::BeginPopup("Loaded Resources"))
            return;
        const std::vector<LoadedResource> loaded = m_assets.resources().loaded();
        if (loaded.empty())
            ImGui::TextDisabled("Nothing is loaded.");
        else
            ImGui::TextDisabled("Released when the last owner lets go.");
        if (!loaded.empty() && ImGui::BeginTable("##Loaded", 3, ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingFixedFit))
        {
            ImGui::TableSetupColumn("Asset");
            ImGui::TableSetupColumn("Type");
            ImGui::TableSetupColumn("Owners");
            ImGui::TableHeadersRow();
            for (const LoadedResource& resource : loaded)
            {
                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                ImGui::TextUnformatted(resource.path.c_str());
                ImGui::TableNextColumn();
                ImGui::TextUnformatted(assetTypeName(resource.type));
                ImGui::TableNextColumn();
                ImGui::Text("%ld", resource.references);
            }
            ImGui::EndTable();
        }
        ImGui::EndPopup();
    }

    void FilesPanel::drawAssetDetails(AssetId asset)
    {
        AssetDatabase& database = m_assets.database();
        const AssetRecord* record = database.find(asset);
        if (!record)
            return;
        if (m_editedAsset != asset)
        {
            m_editedAsset = asset;
            m_modelSettings = record->meta.model;
            m_textureSettings = record->meta.texture;
        }

        ImGui::Separator();
        ImGui::Text("%s  %s", assetTypeName(record->meta.type), record->meta.id.toString().c_str());
        ImGui::SameLine();
        ImGui::TextDisabled("%s", record->metaSaved ? "(saved in its .meta file)" : "(id from the path; settings are not saved)");
        if (!record->metaSaved)
            ImGui::SetItemTooltip("The asset is in a read-only folder or outside assets://, so no .meta file can be written next to it.");
        ImGui::Text("Cooked: %s", assetStateName(record->state));
        if (record->cookedSize > 0)
        {
            ImGui::SameLine();
            ImGui::TextDisabled("%s", formatSize(record->cookedSize).c_str());
        }
        if (record->importMilliseconds > 0.0)
        {
            ImGui::SameLine();
            ImGui::TextDisabled("imported in %.0f ms", record->importMilliseconds);
        }
        if (!record->error.empty())
            ImGui::TextColored({1.0f, 0.45f, 0.4f, 1.0f}, "%s", record->error.c_str());

        if (record->meta.type == AssetType::Model)
        {
            ImGui::DragFloat("Scale", &m_modelSettings.scale, 0.01f, 0.001f, 1000.0f, "%.3f", ImGuiSliderFlags_Logarithmic);
            ImGui::Checkbox("Levels of detail", &m_modelSettings.generateLods);
            ImGui::BeginDisabled(!m_modelSettings.generateLods);
            int levels = static_cast<int>(m_modelSettings.lodLevels);
            if (ImGui::SliderInt("Levels", &levels, 1, static_cast<int>(kMaxLodLevels)))
                m_modelSettings.lodLevels = static_cast<uint32_t>(levels);
            ImGui::SliderFloat("Reduction per level", &m_modelSettings.lodReduction, 0.05f, 0.95f, "%.2f");
            ImGui::EndDisabled();
            compressionCombo("Texture compression", m_modelSettings.textureCompression);
        }
        else if (record->meta.type == AssetType::Texture)
        {
            bool linear = m_textureSettings.colorSpace == ColorSpace::Linear;
            if (ImGui::Checkbox("Linear (data, not colour)", &linear))
                m_textureSettings.colorSpace = linear ? ColorSpace::Linear : ColorSpace::Srgb;
            ImGui::Checkbox("Mip maps", &m_textureSettings.generateMips);
            int maxSize = static_cast<int>(m_textureSettings.maxSize);
            if (ImGui::InputInt("Max size", &maxSize, 256, 1024))
                m_textureSettings.maxSize = static_cast<uint32_t>(std::clamp(maxSize, 1, 16384));
            compressionCombo("Compression", m_textureSettings.compression);
        }

        const bool changed = !(m_modelSettings == record->meta.model) || !(m_textureSettings == record->meta.texture);
        ImGui::BeginDisabled(!changed);
        if (ImGui::Button("Apply"))
        {
            database.setSettings(asset, m_modelSettings, m_textureSettings);
            database.import(asset);
        }
        ImGui::SameLine();
        if (ImGui::Button("Revert"))
            m_editedAsset = {};
        ImGui::EndDisabled();
        ImGui::SameLine();
        if (ImGui::Button("Reimport"))
            database.import(asset, true);
        ImGui::SameLine();
        ImGui::TextDisabled("(?)");
        ImGui::SetItemTooltip("Objects already in the scene keep the version they were loaded with. Import the model "
                              "into the scene again to see the new settings.");
    }
}

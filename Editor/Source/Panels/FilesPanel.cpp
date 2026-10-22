#include "Panels/FilesPanel.h"

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

        const double now = ImGui::GetTime();
        if (ImGui::Button("Refresh") || now - m_cacheTime > kRefreshSeconds)
        {
            m_cache.clear();
            m_cacheTime = now;
        }
        ImGui::SameLine();
        if (ImGui::Button("Mounts"))
            ImGui::OpenPopup("Mounts");
        drawMounts();
        ImGui::SameLine();
        ImGui::TextDisabled("%s", m_selected.empty() ? "Double-click a model to import it" : m_selected.c_str());

        constexpr ImGuiTableFlags kTableFlags = ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV |
                                                ImGuiTableFlags_Resizable | ImGuiTableFlags_ScrollY;
        if (ImGui::BeginTable("##Files", 3, kTableFlags))
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
            if (!isDirectory && isModelFile(child.path))
            {
                if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left) && m_actions.importModel)
                    m_actions.importModel(child.path);
                if (ImGui::BeginDragDropSource())
                {
                    ImGui::SetDragDropPayload(kModelPathPayload, child.path.c_str(), child.path.size() + 1);
                    ImGui::TextUnformatted(child.path.c_str());
                    ImGui::EndDragDropSource();
                }
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
        if (!isDirectory && isModelFile(entry.path))
            if (ImGui::MenuItem("Import") && m_actions.importModel)
                m_actions.importModel(entry.path);
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
}

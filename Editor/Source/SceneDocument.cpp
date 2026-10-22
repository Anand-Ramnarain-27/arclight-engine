#include "SceneDocument.h"

#include "Application/Application.h"
#include "Core/Hash.h"
#include "Core/Log.h"
#include "EditorFiles.h"
#include "EditorSelection.h"
#include "Platform/FileSystem.h"
#include "Platform/VirtualFileSystem.h"
#include "Scene/SceneModule.h"
#include "Scene/SceneSerializer.h"

#include <imgui.h>

namespace arc
{
    namespace
    {
        constexpr const char* kLastSceneFile = "user://LastScene.txt";
        constexpr const char* kUnsavedPopup = "Unsaved Changes";
        constexpr double kDirtyCheckSeconds = 0.5;

        std::string withExtension(std::string path, std::string_view extension)
        {
            if (extensionOf(path) != extension)
                path += extension;
            return path;
        }

        // A native folder for a dialog to start in: the virtual folder if it exists, else assets://.
        std::string dialogFolder(const VirtualFileSystem& fileSystem, std::string_view folder)
        {
            if (fileSystem.isDirectory(folder))
                return fileSystem.nativePath(folder);
            return fileSystem.nativePath("assets://");
        }
    }

    SceneDocument::SceneDocument(Application& app, SceneModule& scene, EditorSelection& selection)
        : m_app(app)
        , m_scene(scene)
        , m_selection(selection)
    {
    }

    uint64_t SceneDocument::sceneHash() const
    {
        return fnv1a64(m_scene.snapshot());
    }

    void SceneDocument::markSaved()
    {
        m_savedHash = sceneHash();
        m_dirty = false;
    }

    bool SceneDocument::isPlaying() const
    {
        return m_app.gameClock().state() != GameClock::State::Stopped;
    }

    std::string SceneDocument::displayName() const
    {
        return m_scene.scenePath().empty() ? std::string("Untitled") : std::string(fileNameOf(m_scene.scenePath()));
    }

    bool SceneDocument::openLastScene()
    {
        const std::optional<std::string> last = m_app.fileSystem().readText(kLastSceneFile);
        if (!last || last->empty())
            return false;
        const std::string path = last->substr(0, last->find_first_of("\r\n"));
        if (!fileExists(path) || !m_scene.openScene(path))
            return false;
        markSaved();
        return true;
    }

    void SceneDocument::rememberScene(const std::string& path)
    {
        m_app.fileSystem().writeText(kLastSceneFile, path + "\n");
    }

    void SceneDocument::update()
    {
        // Play mode: the scene from before Play comes back on Stop, with the same selection.
        const GameClock::State state = m_app.gameClock().state();
        if (m_lastClockState == GameClock::State::Stopped && state != GameClock::State::Stopped)
        {
            m_playSnapshot = m_scene.snapshot();
            m_playSelection = m_selection.object() ? m_selection.object()->uuid() : Uuid{};
        }
        else if (m_lastClockState != GameClock::State::Stopped && state == GameClock::State::Stopped && !m_playSnapshot.empty())
        {
            if (m_scene.restore(m_playSnapshot))
            {
                m_selection.select(m_playSelection ? m_scene.scene().findByUuid(m_playSelection) : nullptr);
                ARC_LOG_INFO("Restored the scene as it was before Play");
            }
            m_playSnapshot.clear();
        }
        m_lastClockState = state;

        // While playing, the scene is not what will be saved.
        const double now = ImGui::GetTime();
        if (!isPlaying() && now >= m_nextDirtyCheck)
        {
            m_dirty = sceneHash() != m_savedHash;
            m_nextDirtyCheck = now + kDirtyCheckSeconds;
        }

        std::string title = "Arclight - " + displayName() + (m_dirty ? "*" : "");
        if (title != m_title)
        {
            m_app.window().setTitle(title);
            m_title = std::move(title);
        }
    }

    void SceneDocument::handleShortcuts()
    {
        if (ImGui::GetIO().WantTextInput)
            return;
        // ImGuiMod_Ctrl is Cmd on macOS.
        if (ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_N))
            runGuarded(Action::New);
        else if (ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_O))
            showOpenDialog();
        else if (ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiMod_Shift | ImGuiKey_S))
            saveAs();
        else if (ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_S))
            save();
    }

    void SceneDocument::drawMenuItems()
    {
        if (ImGui::MenuItem("New Scene", "Ctrl+N"))
            runGuarded(Action::New);
        if (ImGui::MenuItem("Open Scene...", "Ctrl+O"))
            showOpenDialog();
        const bool playing = isPlaying();
        if (ImGui::MenuItem("Save Scene", "Ctrl+S", false, !playing))
            save();
        if (ImGui::MenuItem("Save Scene As...", "Ctrl+Shift+S", false, !playing))
            saveAs();
        if (playing)
            ImGui::SetItemTooltip("Stop the game first: the scene goes back to how it was before Play.");
    }

    void SceneDocument::open(const std::string& path)
    {
        runGuarded(Action::Open, preferredPath(m_app.fileSystem(), path));
    }

    bool SceneDocument::confirmQuit()
    {
        if (!m_dirty)
            return true;
        runGuarded(Action::Quit);
        return false;
    }

    void SceneDocument::quit()
    {
        runGuarded(Action::Quit);
    }

    void SceneDocument::runGuarded(Action action, std::string path)
    {
        if (!m_dirty)
        {
            perform(action, path);
            return;
        }
        m_pendingAction = action;
        m_pendingPath = std::move(path);
        m_openUnsavedPopup = true;
    }

    void SceneDocument::perform(Action action, const std::string& path)
    {
        switch (action)
        {
            case Action::New:
                m_scene.newScene();
                m_selection.clear();
                markSaved();
                break;
            case Action::Open:
                if (m_scene.openScene(path))
                {
                    m_selection.clear();
                    markSaved();
                    rememberScene(path);
                }
                break;
            case Action::Quit:
                m_app.requestQuit();
                break;
            case Action::None:
                break;
        }
    }

    void SceneDocument::save(std::function<void()> then)
    {
        if (isPlaying())
        {
            ARC_LOG_WARN("Stop the game before saving: the scene goes back to how it was before Play");
            return;
        }
        if (m_scene.scenePath().empty())
        {
            saveAs(std::move(then));
            return;
        }
        if (!m_scene.saveScene(m_scene.scenePath()))
            return;
        markSaved();
        rememberScene(m_scene.scenePath());
        if (then)
            then();
    }

    void SceneDocument::saveAs(std::function<void()> then)
    {
        if (isPlaying())
        {
            ARC_LOG_WARN("Stop the game before saving: the scene goes back to how it was before Play");
            return;
        }
        const VirtualFileSystem& fileSystem = m_app.fileSystem();
        FileDialogDesc desc;
        desc.type = FileDialogType::SaveFile;
        desc.title = "Save Scene";
        desc.filters = {{"Arclight scenes", "scene"}};
        desc.defaultLocation = joinPath(dialogFolder(fileSystem, "assets://Scenes"), displayName() == "Untitled"
                                                                                       ? "Untitled.scene"
                                                                                       : displayName());
        m_app.fileDialogs().open(m_app.window().sdlWindow(), std::move(desc),
                                 [this, then = std::move(then)](const FileDialogResult& result) {
                                     if (result.status == FileDialogResult::Status::Failed)
                                         ARC_LOG_WARN("The save dialog is not available ({})", result.error);
                                     if (result.status != FileDialogResult::Status::Chosen)
                                         return;
                                     const std::string path = withExtension(
                                         preferredPath(m_app.fileSystem(), result.paths.front()), kSceneExtension);
                                     if (!m_scene.saveScene(path))
                                         return;
                                     markSaved();
                                     rememberScene(path);
                                     if (then)
                                         then();
                                 });
    }

    void SceneDocument::showOpenDialog()
    {
        FileDialogDesc desc;
        desc.type = FileDialogType::OpenFile;
        desc.title = "Open Scene";
        desc.filters = {{"Arclight scenes", "scene"}};
        desc.defaultLocation = dialogFolder(m_app.fileSystem(), "assets://Scenes");
        m_app.fileDialogs().open(m_app.window().sdlWindow(), std::move(desc), [this](const FileDialogResult& result) {
            if (result.status == FileDialogResult::Status::Failed)
                ARC_LOG_WARN("The open dialog is not available ({}); double-click a .scene file in the Files panel instead",
                             result.error);
            if (result.status == FileDialogResult::Status::Chosen)
                open(result.paths.front());
        });
    }

    void SceneDocument::savePrefabAs(const GameObject& object)
    {
        const GameObjectHandle handle = object.handle();
        FileDialogDesc desc;
        desc.type = FileDialogType::SaveFile;
        desc.title = "Save as Prefab";
        desc.filters = {{"Arclight prefabs", "prefab"}};
        desc.defaultLocation = joinPath(dialogFolder(m_app.fileSystem(), "assets://Prefabs"), object.name() + kPrefabExtension);
        m_app.fileDialogs().open(m_app.window().sdlWindow(), std::move(desc), [this, handle](const FileDialogResult& result) {
            if (result.status == FileDialogResult::Status::Failed)
                ARC_LOG_WARN("The save dialog is not available ({})", result.error);
            if (result.status != FileDialogResult::Status::Chosen)
                return;
            // The object may have been deleted while the dialog was open.
            const GameObject* target = m_scene.scene().find(handle);
            if (!target)
                return;
            m_scene.savePrefab(*target, withExtension(preferredPath(m_app.fileSystem(), result.paths.front()), kPrefabExtension));
        });
    }

    void SceneDocument::drawPopups()
    {
        if (m_openUnsavedPopup)
        {
            ImGui::OpenPopup(kUnsavedPopup);
            m_openUnsavedPopup = false;
        }
        if (!ImGui::BeginPopupModal(kUnsavedPopup, nullptr, ImGuiWindowFlags_AlwaysAutoResize))
            return;

        ImGui::Text("Save the changes to %s?", displayName().c_str());
        ImGui::TextDisabled("They are lost otherwise.");
        ImGui::Spacing();
        const Action action = m_pendingAction;
        const std::string path = m_pendingPath;
        bool close = false;
        if (ImGui::Button("Save"))
        {
            save([this, action, path] { perform(action, path); });
            close = true;
        }
        ImGui::SameLine();
        if (ImGui::Button("Don't Save"))
        {
            perform(action, path);
            close = true;
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel") || ImGui::IsKeyPressed(ImGuiKey_Escape))
            close = true;
        if (close)
        {
            m_pendingAction = Action::None;
            m_pendingPath.clear();
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
}

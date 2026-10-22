#pragma once

#include "Core/Time.h"
#include "Scene/GameObject.h"

#include <cstdint>
#include <functional>
#include <string>

namespace arc
{
    class Application;
    class EditorSelection;
    class SceneModule;

    /// The scene as a document: its file, whether it has unsaved changes, and the actions that
    /// replace it (New, Open) or end the program, which first offer to save. Also keeps Play mode
    /// from changing the scene for good: the scene is snapshotted when the game clock leaves
    /// Stopped and restored when it returns, so whatever happens while playing is undone.
    ///
    /// Unsaved changes are found by comparing a hash of the scene as it would be saved with the
    /// hash at the last save, a few times a second, rather than by tracking every edit: nothing
    /// that changes the scene can be missed, and undoing a change by hand clears the mark.
    class SceneDocument
    {
    public:
        SceneDocument(Application& app, SceneModule& scene, EditorSelection& selection);

        /// Opens the scene that was open when the editor last closed. False if there is none, or it
        /// cannot be opened.
        bool openLastScene();
        /// The scene as it is now counts as saved.
        void markSaved();

        /// Called once per frame, before the panels draw.
        void update();
        void handleShortcuts();
        /// New Scene, Open Scene, Save Scene and Save Scene As, for the File menu.
        void drawMenuItems();
        void drawPopups();

        /// For a scene file chosen anywhere in the editor; offers to save the current scene first.
        void open(const std::string& path);
        /// Asks where to save `object`'s subtree as a prefab.
        void savePrefabAs(const GameObject& object);
        /// For the window's close button: true to quit now; false after offering to save first.
        bool confirmQuit();
        /// For File > Exit.
        void quit();

        [[nodiscard]] bool isDirty() const { return m_dirty; }
        /// The file name, or "Untitled".
        [[nodiscard]] std::string displayName() const;

    private:
        enum class Action : uint8_t
        {
            None,
            New,
            Open,
            Quit,
        };

        void runGuarded(Action action, std::string path = {});
        void perform(Action action, const std::string& path);
        void save(std::function<void()> then = {});
        void saveAs(std::function<void()> then = {});
        void showOpenDialog();
        void rememberScene(const std::string& path);
        [[nodiscard]] bool isPlaying() const;
        [[nodiscard]] uint64_t sceneHash() const;

        Application& m_app;
        SceneModule& m_scene;
        EditorSelection& m_selection;

        uint64_t m_savedHash = 0;
        bool m_dirty = false;
        double m_nextDirtyCheck = 0.0;
        std::string m_title;

        Action m_pendingAction = Action::None;
        std::string m_pendingPath;
        bool m_openUnsavedPopup = false;

        GameClock::State m_lastClockState = GameClock::State::Stopped;
        std::string m_playSnapshot;
        Uuid m_playSelection;
    };
}

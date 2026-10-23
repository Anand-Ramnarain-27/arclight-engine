#pragma once

#include "Scene/ComponentData.h"

#include <string>
#include <string_view>

namespace arc
{
    class ComponentRegistry;
    class GameObject;
    class Scene;

    /// Raised when the format of scene or prefab files changes; older files are still read.
    inline constexpr int kSceneFormatVersion = 1;

    inline constexpr const char* kSceneExtension = ".scene";
    inline constexpr const char* kPrefabExtension = ".prefab";

    struct SceneLoadOptions
    {
        ComponentLoadContext context;
        /// Null for ComponentRegistry::engine().
        const ComponentRegistry* components = nullptr;
    };

    // Scenes and prefabs are JSON: a list of objects, parents before children and siblings in
    // order, each with its UUID, name, active flag, local transform and components. A component is
    // its type name, its enabled flag and whatever Component::save() wrote. Plain text, so files
    // can be merged and reviewed in version control.

    /// Every object in the scene.
    [[nodiscard]] std::string writeScene(const Scene& scene);

    /// Replaces the scene's contents with the file's. The whole file is checked before anything
    /// changes: on failure the scene is left as it was, `error` says why, and false is returned.
    /// Objects whose parent is not in the file go to the top level, a repeated UUID is replaced by a
    /// new one, and a component of an unknown type becomes a MissingComponent; each is logged.
    bool readScene(Scene& scene, std::string_view json, const SceneLoadOptions& options, std::string* error = nullptr);

    /// `root` and everything below it, as a prefab: the same format, with `root` first.
    [[nodiscard]] std::string writePrefab(const GameObject& root);

    /// A new copy of the prefab at the end of `parent`'s children (or the top level). Every object
    /// gets a new UUID, so a prefab can be placed any number of times. Null on failure.
    GameObject* instantiatePrefab(Scene& scene, std::string_view json, GameObject* parent, const SceneLoadOptions& options,
                                  std::string* error = nullptr);
}

#pragma once

#include <memory>

namespace arc
{
    class GameObject;
    class Model;
    class Scene;

    /// Recreates the model's node hierarchy as GameObjects, under a new object named after the
    /// model, and returns that object. Nodes with a mesh get a MeshComponent that shares `model`.
    /// The new object goes at the end of `parent`'s children (or the top level).
    GameObject& instantiateModel(Scene& scene, const std::shared_ptr<const Model>& model, GameObject* parent = nullptr);
}

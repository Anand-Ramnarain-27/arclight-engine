#pragma once

#include "Math/Aabb.h"
#include "Scene/Component.h"

#include <cstdint>
#include <memory>

namespace arc
{
    class Model;

    /// Draws one mesh of a Model at its GameObject's transform. The model is shared: every
    /// component that uses it keeps it alive, and the last one to let go releases it.
    class MeshComponent final : public Component
    {
        ARC_COMPONENT(MeshComponent, "Mesh")

    public:
        MeshComponent() = default;
        MeshComponent(std::shared_ptr<const Model> model, uint32_t mesh);

        void setMesh(std::shared_ptr<const Model> model, uint32_t mesh);

        /// Null until a mesh is set.
        [[nodiscard]] const std::shared_ptr<const Model>& model() const { return m_model; }
        [[nodiscard]] uint32_t meshIndex() const { return m_mesh; }

        /// A model is set and the index is one of its meshes.
        [[nodiscard]] bool hasMesh() const;

        /// Object space. Empty without a mesh.
        [[nodiscard]] Aabb localBounds() const;

    private:
        std::shared_ptr<const Model> m_model;
        uint32_t m_mesh = 0;
    };

    /// World space: the bounds of every mesh in the object's subtree that would be drawn. Empty if
    /// there is none.
    [[nodiscard]] Aabb hierarchyBounds(const GameObject& object);
}

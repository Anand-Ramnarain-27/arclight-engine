#include "Scene/MeshComponent.h"

#include "Renderer/Model.h"
#include "Scene/GameObject.h"

#include <utility>

namespace arc
{
    MeshComponent::MeshComponent(std::shared_ptr<const Model> model, uint32_t mesh)
        : m_model(std::move(model))
        , m_mesh(mesh)
    {
    }

    void MeshComponent::setMesh(std::shared_ptr<const Model> model, uint32_t mesh)
    {
        m_model = std::move(model);
        m_mesh = mesh;
    }

    bool MeshComponent::hasMesh() const
    {
        return m_model && m_mesh < m_model->meshes().size();
    }

    Aabb MeshComponent::localBounds() const
    {
        return hasMesh() ? m_model->meshBounds(m_mesh) : Aabb{};
    }

    namespace
    {
        void expandByHierarchy(Aabb& bounds, const GameObject& object)
        {
            if (!object.isActiveSelf())
                return;
            const Mat4& world = object.transform().worldMatrix();
            for (const std::unique_ptr<Component>& component : object.components())
                if (component->enabled && &component->type() == &MeshComponent::staticType())
                    bounds.expand(static_cast<const MeshComponent&>(*component).localBounds().transformed(world));
            for (const GameObject* child : object.children())
                expandByHierarchy(bounds, *child);
        }
    }

    Aabb hierarchyBounds(const GameObject& object)
    {
        Aabb bounds;
        expandByHierarchy(bounds, object);
        return bounds;
    }
}

#include "Scene/MeshComponent.h"

#include "Assets/ResourceManager.h"
#include "Core/Log.h"
#include "Renderer/Model.h"
#include "Scene/ComponentData.h"
#include "Scene/GameObject.h"

#include <utility>

namespace arc
{
    MeshComponent::MeshComponent(std::shared_ptr<const Model> model, uint32_t mesh)
    {
        setMesh(std::move(model), mesh);
    }

    void MeshComponent::setMesh(std::shared_ptr<const Model> model, uint32_t mesh)
    {
        m_asset = model ? model->asset() : Uuid{};
        m_model = std::move(model);
        m_mesh = mesh;
    }

    void MeshComponent::save(ComponentData& data) const
    {
        if (m_asset)
            data.setUuid("model", m_asset);
        data.setInt("mesh", m_mesh);
    }

    void MeshComponent::load(const ComponentData& data, const ComponentLoadContext& context)
    {
        if (const std::optional<int64_t> mesh = data.getInt("mesh"); mesh && *mesh >= 0)
            m_mesh = static_cast<uint32_t>(*mesh);
        const std::optional<Uuid> asset = data.getUuid("model");
        if (!asset)
            return;
        m_asset = *asset;
        m_model = context.resources ? context.resources->loadModel(*asset) : nullptr;
        if (!m_model && context.resources)
            ARC_LOG_WARN("The model {} of a Mesh component could not be loaded; the reference is kept", asset->toString());
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

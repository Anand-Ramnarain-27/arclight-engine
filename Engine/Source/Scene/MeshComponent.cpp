#include "Scene/MeshComponent.h"

#include "Assets/ResourceManager.h"
#include "Core/Log.h"
#include "Renderer/Model.h"
#include "Scene/ComponentData.h"
#include "Scene/GameObject.h"

#include <algorithm>
#include <format>
#include <string_view>
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

    namespace
    {
        // Material styles are saved as flat keys, "material2.bands" and so on, for the materials
        // whose style is not the default; "materialStyles" is one past the last of them.
        constexpr uint32_t kMaxSavedStyles = 4096;

        std::string styleKey(uint32_t material, std::string_view field)
        {
            return std::format("material{}.{}", material, field);
        }
    }

    const MaterialStyle& MeshComponent::materialStyle(uint32_t material) const
    {
        static const MaterialStyle defaultStyle;
        return material < m_styles.size() ? m_styles[material] : defaultStyle;
    }

    void MeshComponent::growStyles(uint32_t material)
    {
        if (material >= m_styles.size())
        {
            m_styles.resize(size_t{material} + 1);
            m_ramps.resize(size_t{material} + 1);
        }
    }

    void MeshComponent::setMaterialStyle(uint32_t material, const MaterialStyle& style)
    {
        growStyles(material);
        rhi::Texture* const ramp = m_styles[material].toon.ramp;
        m_styles[material] = style;
        m_styles[material].toon.ramp = ramp;
    }

    void MeshComponent::setToonRamp(uint32_t material, std::string path, std::shared_ptr<const TextureAsset> texture)
    {
        growStyles(material);
        m_styles[material].toon.ramp = texture ? texture->texture.get() : nullptr;
        m_ramps[material] = {std::move(path), std::move(texture)};
    }

    const std::string& MeshComponent::toonRampPath(uint32_t material) const
    {
        static const std::string none;
        return material < m_ramps.size() ? m_ramps[material].path : none;
    }

    void MeshComponent::save(ComponentData& data) const
    {
        if (m_asset)
            data.setUuid("model", m_asset);
        data.setInt("mesh", m_mesh);

        uint32_t saved = 0;
        for (uint32_t i = 0; i < m_styles.size() && i < kMaxSavedStyles; ++i)
        {
            const MaterialStyle& style = m_styles[i];
            if (style.isDefault() && m_ramps[i].path.empty())
                continue;
            saved = i + 1;
            data.setString(styleKey(i, "shading"), style.shading == ShadingModel::Toon ? "toon" : "physical");
            data.setInt(styleKey(i, "bands"), style.toon.bands);
            data.setFloat(styleKey(i, "softness"), style.toon.softness);
            data.setFloat(styleKey(i, "specular"), style.toon.specular);
            data.setFloat(styleKey(i, "rimSize"), style.toon.rimSize);
            data.setFloat(styleKey(i, "rim"), style.toon.rim);
            if (!m_ramps[i].path.empty())
                data.setString(styleKey(i, "ramp"), m_ramps[i].path);
            data.setBool(styleKey(i, "outline"), style.outline.enabled);
            data.setFloat(styleKey(i, "outlineWidth"), style.outline.width);
            data.setVec3(styleKey(i, "outlineColor"), style.outline.color);
        }
        if (saved > 0)
            data.setInt("materialStyles", saved);
    }

    void MeshComponent::load(const ComponentData& data, const ComponentLoadContext& context)
    {
        if (const std::optional<int64_t> mesh = data.getInt("mesh"); mesh && *mesh >= 0)
            m_mesh = static_cast<uint32_t>(*mesh);

        // Values out of range are clamped, as the Inspector would, so a hand-edited file still loads.
        const int64_t styleCount = std::clamp<int64_t>(data.getInt("materialStyles").value_or(0), 0, kMaxSavedStyles);
        for (uint32_t i = 0; i < static_cast<uint32_t>(styleCount); ++i)
        {
            const std::optional<std::string> shading = data.getString(styleKey(i, "shading"));
            if (!shading)
                continue;
            MaterialStyle style;
            style.shading = *shading == "toon" ? ShadingModel::Toon : ShadingModel::Physical;
            ToonStyle& toon = style.toon;
            toon.bands = static_cast<uint32_t>(std::clamp<int64_t>(data.getInt(styleKey(i, "bands")).value_or(toon.bands), 1, 8));
            toon.softness = std::clamp(data.getFloat(styleKey(i, "softness")).value_or(toon.softness), 0.0f, 0.5f);
            toon.specular = std::max(data.getFloat(styleKey(i, "specular")).value_or(toon.specular), 0.0f);
            toon.rimSize = std::clamp(data.getFloat(styleKey(i, "rimSize")).value_or(toon.rimSize), 0.0f, 1.0f);
            toon.rim = std::max(data.getFloat(styleKey(i, "rim")).value_or(toon.rim), 0.0f);
            style.outline.enabled = data.getBool(styleKey(i, "outline")).value_or(false);
            style.outline.width = std::clamp(data.getFloat(styleKey(i, "outlineWidth")).value_or(style.outline.width), 0.0f, 32.0f);
            style.outline.color = data.getVec3(styleKey(i, "outlineColor")).value_or(style.outline.color);
            setMaterialStyle(i, style);

            if (std::optional<std::string> ramp = data.getString(styleKey(i, "ramp")); ramp && !ramp->empty())
            {
                std::shared_ptr<const TextureAsset> texture =
                    context.resources ? context.resources->loadTexture(*ramp) : nullptr;
                if (!texture && context.resources)
                    ARC_LOG_WARN("Toon ramp '{}' could not be loaded; the material's light is cut into bands", *ramp);
                setToonRamp(i, std::move(*ramp), std::move(texture));
            }
        }
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

    namespace
    {
        // Calls edit(mesh, material) for each material the mesh's primitives draw, once each.
        template <typename Edit>
        uint32_t forEachDrawnMaterial(GameObject& object, const Edit& edit)
        {
            uint32_t changed = 0;
            for (const std::unique_ptr<Component>& component : object.components())
            {
                if (&component->type() != &MeshComponent::staticType())
                    continue;
                auto& mesh = static_cast<MeshComponent&>(*component);
                if (!mesh.hasMesh())
                    continue;
                const Model& model = *mesh.model();
                const MeshData& data = model.meshes()[mesh.meshIndex()];
                std::vector<uint32_t> seen;
                for (uint32_t i = 0; i < data.primitiveCount; ++i)
                {
                    const int32_t material = model.primitives()[data.firstPrimitive + i].material;
                    if (material < 0 || std::find(seen.begin(), seen.end(), static_cast<uint32_t>(material)) != seen.end())
                        continue;
                    seen.push_back(static_cast<uint32_t>(material));
                    if (edit(mesh, static_cast<uint32_t>(material)))
                        ++changed;
                }
            }
            for (GameObject* child : object.children())
                changed += forEachDrawnMaterial(*child, edit);
            return changed;
        }
    }

    uint32_t changeStyles(GameObject& object, const StyleChange& change)
    {
        return forEachDrawnMaterial(object, [&](MeshComponent& mesh, uint32_t material) {
            MaterialStyle style = mesh.materialStyle(material);
            const MaterialStyle before = style;
            if (change.shading)
                style.shading = *change.shading;
            if (change.outline)
                style.outline.enabled = *change.outline;
            if (style == before)
                return false;
            mesh.setMaterialStyle(material, style);
            return true;
        });
    }

    uint32_t copyStyleToHierarchy(GameObject& object, const MaterialStyle& style)
    {
        return forEachDrawnMaterial(object, [&](MeshComponent& mesh, uint32_t material) {
            MaterialStyle copy = style;
            copy.toon.ramp = mesh.materialStyle(material).toon.ramp;
            if (copy == mesh.materialStyle(material))
                return false;
            mesh.setMaterialStyle(material, copy);
            return true;
        });
    }
}

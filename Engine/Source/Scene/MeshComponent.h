#pragma once

#include "Math/Aabb.h"
#include "Renderer/MaterialStyle.h"
#include "Scene/Component.h"
#include "Scene/Uuid.h"

#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace arc
{
    class Model;
    struct TextureAsset;

    /// Draws one mesh of a Model at its GameObject's transform. The model is shared: every
    /// component that uses it keeps it alive, and the last one to let go releases it.
    ///
    /// Each of the model's materials can be given a style for this object (toon shading, outlines);
    /// the model itself, shared by every object that draws it, is never changed.
    ///
    /// Saved as the model's asset id, the mesh index and the styles that differ from the default.
    /// The id is kept even when the model cannot be loaded, so saving the scene again does not lose
    /// the reference.
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
        /// The asset of the model; null for a model not loaded from an asset.
        [[nodiscard]] Uuid modelAsset() const { return m_asset; }

        /// A model is set and the index is one of its meshes.
        [[nodiscard]] bool hasMesh() const;

        /// Object space. Empty without a mesh.
        [[nodiscard]] Aabb localBounds() const;

        /// Indexed by material; may be shorter than the model's materials, the rest being default.
        [[nodiscard]] std::span<const MaterialStyle> materialStyles() const { return m_styles; }
        /// The default style for a material without one.
        [[nodiscard]] const MaterialStyle& materialStyle(uint32_t material) const;
        /// The toon ramp is kept: it changes only through setToonRamp().
        void setMaterialStyle(uint32_t material, const MaterialStyle& style);
        /// A texture asset's path, or empty to cut light into bands again. `texture` keeps the
        /// texture alive for as long as the style uses it; null with a path when it could not be
        /// loaded, so the path is still saved.
        void setToonRamp(uint32_t material, std::string path, std::shared_ptr<const TextureAsset> texture);
        [[nodiscard]] const std::string& toonRampPath(uint32_t material) const;

        void save(ComponentData& data) const override;
        void load(const ComponentData& data, const ComponentLoadContext& context) override;

    private:
        struct Ramp
        {
            std::string path;
            std::shared_ptr<const TextureAsset> texture;
        };

        void growStyles(uint32_t material);

        std::shared_ptr<const Model> m_model;
        uint32_t m_mesh = 0;
        Uuid m_asset;
        std::vector<MaterialStyle> m_styles;
        std::vector<Ramp> m_ramps;   // Indexed like m_styles.
    };

    /// World space: the bounds of every mesh in the object's subtree that would be drawn. Empty if
    /// there is none.
    [[nodiscard]] Aabb hierarchyBounds(const GameObject& object);

    /// An edit to many materials' styles at once; what is left unset stays as each material has it,
    /// so a room whose materials were tuned one by one can be switched to toon without losing that.
    struct StyleChange
    {
        std::optional<ShadingModel> shading;
        std::optional<bool> outline;
    };

    /// Applies `change` to every material drawn by every Mesh component in the object's subtree, the
    /// object included, inactive objects too. Returns the number of materials changed.
    uint32_t changeStyles(GameObject& object, const StyleChange& change);

    /// Gives every material drawn in the object's subtree `style`, except for its ramp texture, which
    /// each keeps. Returns the number of materials changed.
    uint32_t copyStyleToHierarchy(GameObject& object, const MaterialStyle& style);
}

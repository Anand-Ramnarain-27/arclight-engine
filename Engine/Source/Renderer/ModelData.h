#pragma once

#include "Math/Aabb.h"
#include "Math/Math.h"
#include "RHI/RHITypes.h"
#include "Renderer/Image.h"

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace arc
{
    // A model as imported, in CPU memory: what an importer produces and Model::create() uploads.
    // Kept free of GPU objects so importers can be tested without a device.

    /// The vertex every mesh is converted to. Matches VSInput in Engine/Shaders/Mesh.hlsl.
    struct MeshVertex
    {
        Vec3 position;   // Object space.
        Vec3 normal;     // Object space, unit length.
        Vec2 uv;         // TEXCOORD_0; (0, 0) is the top-left of the image.
    };
    static_assert(sizeof(MeshVertex) == 32, "MeshVertex must be tightly packed to match the vertex layout");

    /// How a material's alpha is used. The values match AlphaMode in Mesh.hlsl.
    enum class AlphaMode : uint32_t
    {
        /// Alpha is ignored.
        Opaque = 0,
        /// Pixels with alpha below the material's cutoff are discarded; the rest are opaque.
        Mask = 1,
        /// Blended over what is behind, in draw order (no sorting yet).
        Blend = 2,
    };

    /// The glTF metallic-roughness material. Each factor multiplies its texture; without the texture
    /// the factor alone is the value.
    struct MaterialData
    {
        std::string name;
        Vec4 baseColorFactor{1.0f, 1.0f, 1.0f, 1.0f};   // Linear RGB, and alpha.
        int32_t baseColorTexture = -1;                   // Into ModelData::textures; -1 for none.
        float metallicFactor = 1.0f;
        float roughnessFactor = 1.0f;
        /// Linear data: blue is metallic, green is roughness (red is unused, or occlusion in packed
        /// ORM textures). -1 for none.
        int32_t metallicRoughnessTexture = -1;
        AlphaMode alphaMode = AlphaMode::Opaque;
        float alphaCutoff = 0.5f;
        /// Back faces are drawn too, lit with their normal flipped.
        bool doubleSided = false;
    };

    struct TextureData
    {
        int32_t image = -1;   // Into ModelData::images.
        rhi::SamplerDesc sampler;
    };

    /// A simplified version of a primitive: fewer triangles over the same vertices.
    struct LodLevel
    {
        uint32_t firstIndex = 0;   // Into ModelData::indices, relative to the primitive's baseVertex.
        uint32_t indexCount = 0;
        /// Object-space distance the simplified surface may be from the original.
        float error = 0.0f;
    };

    /// Simplified levels a primitive can have besides its full-detail one.
    inline constexpr uint32_t kMaxLodLevels = 3;

    /// One draw: a run of indices into the model's shared buffers, with one material.
    struct Primitive
    {
        uint32_t firstIndex = 0;
        uint32_t indexCount = 0;
        /// Added to every index; the index values themselves start at 0 for each primitive.
        uint32_t baseVertex = 0;
        uint32_t vertexCount = 0;
        int32_t material = -1;   // Into ModelData::materials; -1 for the default material.
        Aabb bounds;             // Object space.
        /// Coarser and coarser versions, filled by generateLods(); the first lodCount are valid.
        std::array<LodLevel, kMaxLodLevels> lods{};
        uint32_t lodCount = 0;
    };

    struct MeshData
    {
        std::string name;
        uint32_t firstPrimitive = 0;   // Into ModelData::primitives.
        uint32_t primitiveCount = 0;
    };

    /// A mesh placed in the model by a node. The node hierarchy is flattened on import: each
    /// instance carries its node's complete transform.
    struct MeshInstance
    {
        std::string name;   // The node's.
        Mat4 world;         // Object space to model space.
        uint32_t mesh = 0;  // Into ModelData::meshes.
    };

    /// A node of the glTF scene, kept with its hierarchy so a scene can recreate it as objects.
    /// A node's matrix, when the file gives one, is decomposed into translation, rotation and scale.
    struct NodeData
    {
        std::string name;
        Vec3 translation;
        Quat rotation;
        Vec3 scale = Vec3::one();
        int32_t parent = -1;   // Into ModelData::nodes; -1 for a root. Always lower than this node's index.
        int32_t mesh = -1;     // Into ModelData::meshes; -1 for a node without one.
    };

    struct ModelData
    {
        std::string name;

        /// Every primitive's vertices and indices, end to end, so the model needs one vertex buffer
        /// and one index buffer.
        std::vector<MeshVertex> vertices;
        std::vector<uint32_t> indices;

        std::vector<Primitive> primitives;
        std::vector<MeshData> meshes;
        std::vector<MeshInstance> instances;
        /// The default scene's nodes, parents before children.
        std::vector<NodeData> nodes;
        std::vector<MaterialData> materials;
        std::vector<TextureData> textures;
        /// Decoded RGBA8. Only images used by a material slot that is drawn are decoded; the rest
        /// stay empty.
        std::vector<Image> images;
        /// How each image is stored, from the material slot that uses it: sRGB for base colour,
        /// linear for data. Indexed like `images` and `imageMips`.
        std::vector<ColorSpace> imageColorSpaces;
        /// Every mip level of each image, level 0 first, when they were made ahead of time (cooked
        /// models have these and no `images`). Model::create() uploads these as they are, and builds
        /// the levels from `images` only for an image that has none here.
        std::vector<std::vector<Image>> imageMips;

        /// Model space: every instance's primitives, transformed.
        Aabb bounds;
    };
}

#pragma once

#include "Renderer/ModelData.h"

#include <cstdint>
#include <span>
#include <vector>

namespace arc
{
    /// Fills every vertex's tangent with MikkTSpace, the tangent generator glTF names for files
    /// without tangents, which is also what most bakers use: a normal map only looks right with the
    /// tangent frame it was baked against. `indices` is a triangle list into `vertices`.
    ///
    /// A vertex whose triangles need different tangents (on a UV seam, or where the texture is
    /// mirrored) is split in two, and identical vertices are merged, so both arrays may change.
    void generateTangents(std::vector<MeshVertex>& vertices, std::vector<uint32_t>& indices);

    /// Some unit tangent perpendicular to each normal, with w = 1: for meshes without texture
    /// coordinates or a normal map, whose shading does not depend on it.
    void fillDefaultTangents(std::span<MeshVertex> vertices);
}

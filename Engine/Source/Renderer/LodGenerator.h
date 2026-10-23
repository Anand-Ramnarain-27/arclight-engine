#pragma once

#include "Renderer/ModelData.h"

namespace arc
{
    struct LodSettings
    {
        /// Each level aims for this fraction of the previous level's triangles.
        float reduction = 0.5f;
        /// Primitives with fewer triangles than this get no simplified levels.
        uint32_t minTriangles = 64;
        /// A level that removes less than this fraction of the triangles is not kept, and no
        /// coarser levels are made: the simplifier has reached what it can do without breaking the
        /// primitive's borders.
        float minReduction = 0.1f;
        /// At most kMaxLodLevels.
        uint32_t maxLevels = kMaxLodLevels;
    };

    /// Adds up to settings.maxLevels simplified levels to every primitive, with meshoptimizer: edges are
    /// collapsed where that moves the surface least, until the target triangle count is reached.
    /// Each primitive's open borders are locked, so primitives that meet along a seam (one mesh
    /// split by material) never open cracks between them. Appends the levels' indices to
    /// data.indices.
    void generateLods(ModelData& data, const LodSettings& settings = {});
}

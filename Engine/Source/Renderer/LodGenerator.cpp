#include "Renderer/LodGenerator.h"

#include <meshoptimizer.h>

#include <algorithm>
#include <cstddef>
#include <vector>

namespace arc
{
    void generateLods(ModelData& data, const LodSettings& settings)
    {
        std::vector<uint32_t> source;
        std::vector<uint32_t> simplified;
        for (Primitive& primitive : data.primitives)
        {
            primitive.lodCount = 0;
            if (primitive.indexCount / 3 < settings.minTriangles || primitive.vertexCount == 0)
                continue;

            // meshoptimizer reads positions straight out of the interleaved vertices.
            const float* positions = &data.vertices[primitive.baseVertex].position.x;
            const size_t stride = sizeof(MeshVertex);
            // Its error is relative to the primitive's size; this scale turns it into object units.
            const float scale = meshopt_simplifyScale(positions, primitive.vertexCount, stride);

            source.assign(data.indices.begin() + primitive.firstIndex,
                          data.indices.begin() + primitive.firstIndex + primitive.indexCount);
            size_t previousCount = source.size();
            float target = static_cast<float>(source.size());
            for (uint32_t level = 0; level < std::min(settings.maxLevels, kMaxLodLevels); ++level)
            {
                target *= settings.reduction;
                const size_t targetCount = std::max<size_t>(3, static_cast<size_t>(target) / 3 * 3);
                simplified.resize(source.size());
                float error = 0.0f;
                // Always simplified from the full-detail indices, so errors do not compound from
                // level to level. An error limit of 1 (the whole primitive) lets the triangle count
                // decide.
                const size_t count = meshopt_simplify(simplified.data(), source.data(), source.size(), positions,
                                                      primitive.vertexCount, stride, targetCount, 1.0f,
                                                      meshopt_SimplifyLockBorder, &error);
                if (count == 0 || static_cast<float>(count) > static_cast<float>(previousCount) * (1.0f - settings.minReduction))
                    break;

                primitive.lods[level] = {.firstIndex = static_cast<uint32_t>(data.indices.size()),
                                         .indexCount = static_cast<uint32_t>(count),
                                         .error = error * scale};
                data.indices.insert(data.indices.end(), simplified.begin(), simplified.begin() + static_cast<std::ptrdiff_t>(count));
                primitive.lodCount = level + 1;
                previousCount = count;
            }
        }
    }
}

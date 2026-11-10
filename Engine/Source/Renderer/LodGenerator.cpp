#include "Renderer/LodGenerator.h"

#include "Core/JobSystem.h"
#include "Core/Profiler.h"

#include <meshoptimizer.h>

#include <algorithm>
#include <cstddef>
#include <vector>

namespace arc
{
    void generateLods(ModelData& data, const LodSettings& settings)
    {
        ARC_PROFILE_SCOPE("Generate LODs");

        struct Level
        {
            std::vector<uint32_t> indices;
            float error = 0.0f;
        };
        // Primitives are simplified in parallel, each into its own levels; they are appended to the
        // shared index buffer afterwards, in primitive order, so the result is the same on any number
        // of threads.
        std::vector<std::vector<Level>> results(data.primitives.size());
        const uint32_t levelLimit = std::min(settings.maxLevels, kMaxLodLevels);

        parallelFor(static_cast<uint32_t>(data.primitives.size()), 1, [&](uint32_t first, uint32_t end) {
            std::vector<uint32_t> simplified;
            for (uint32_t p = first; p < end; ++p)
            {
                const Primitive& primitive = data.primitives[p];
                if (primitive.indexCount / 3 < settings.minTriangles || primitive.vertexCount == 0)
                    continue;

                // meshoptimizer reads positions straight out of the interleaved vertices.
                const float* positions = &data.vertices[primitive.baseVertex].position.x;
                const size_t stride = sizeof(MeshVertex);
                // Its error is relative to the primitive's size; this scale turns it into object units.
                const float scale = meshopt_simplifyScale(positions, primitive.vertexCount, stride);

                const uint32_t* source = data.indices.data() + primitive.firstIndex;
                const size_t sourceCount = primitive.indexCount;
                size_t previousCount = sourceCount;
                float target = static_cast<float>(sourceCount);
                for (uint32_t level = 0; level < levelLimit; ++level)
                {
                    target *= settings.reduction;
                    const size_t targetCount = std::max<size_t>(3, static_cast<size_t>(target) / 3 * 3);
                    simplified.resize(sourceCount);
                    float error = 0.0f;
                    // Always simplified from the full-detail indices, so errors do not compound from
                    // level to level. An error limit of 1 (the whole primitive) lets the triangle count
                    // decide.
                    const size_t count = meshopt_simplify(simplified.data(), source, sourceCount, positions,
                                                          primitive.vertexCount, stride, targetCount, 1.0f,
                                                          meshopt_SimplifyLockBorder, &error);
                    if (count == 0 ||
                        static_cast<float>(count) > static_cast<float>(previousCount) * (1.0f - settings.minReduction))
                        break;
                    results[p].push_back({.indices = std::vector<uint32_t>(simplified.begin(), simplified.begin() +
                                                                                       static_cast<std::ptrdiff_t>(count)),
                                          .error = error * scale});
                    previousCount = count;
                }
            }
        });

        for (size_t p = 0; p < data.primitives.size(); ++p)
        {
            Primitive& primitive = data.primitives[p];
            primitive.lodCount = static_cast<uint32_t>(results[p].size());
            for (size_t level = 0; level < results[p].size(); ++level)
            {
                const Level& result = results[p][level];
                primitive.lods[level] = {.firstIndex = static_cast<uint32_t>(data.indices.size()),
                                         .indexCount = static_cast<uint32_t>(result.indices.size()),
                                         .error = result.error};
                data.indices.insert(data.indices.end(), result.indices.begin(), result.indices.end());
            }
        }
    }
}

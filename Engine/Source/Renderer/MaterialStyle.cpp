#include "Renderer/MaterialStyle.h"

#include <algorithm>

namespace arc
{
    const char* shadingModelName(ShadingModel model)
    {
        switch (model)
        {
            case ShadingModel::Physical: return "Physical";
            case ShadingModel::Toon:     return "Toon";
        }
        return "";
    }

    float toonBandLight(float nDotL, uint32_t bands, float softness)
    {
        const uint32_t count = std::max(bands, 1u);
        const float halfWidth = std::max(softness, 1e-4f);
        float tone = 0.0f;
        for (uint32_t band = 0; band < count; ++band)
        {
            const float edge = static_cast<float>(band) / static_cast<float>(count);
            const float t = std::clamp((nDotL - (edge - halfWidth)) / (2.0f * halfWidth), 0.0f, 1.0f);
            tone += t * t * (3.0f - 2.0f * t);
        }
        return tone / static_cast<float>(count);
    }
}

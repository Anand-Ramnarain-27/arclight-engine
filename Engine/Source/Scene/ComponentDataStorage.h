#pragma once

// Internal to the engine: the storage behind ComponentData. Included only by .cpp files, so the
// JSON library does not reach any public header.

#include "Scene/ComponentData.h"

// nlohmann/json, as vendored by tinygltf.
#include <json.hpp>

namespace arc
{
    struct ComponentData::Storage
    {
        nlohmann::ordered_json object = nlohmann::ordered_json::object();
    };

    /// The float as the double with the fewest decimal digits that still reads back as the same
    /// float: 0.1f is written as 0.1, not 0.100000001490116.
    [[nodiscard]] double shortestDouble(float value);
}

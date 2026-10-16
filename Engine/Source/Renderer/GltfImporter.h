#pragma once

#include "Renderer/ModelData.h"

#include <cstddef>
#include <optional>
#include <span>
#include <string>
#include <string_view>

namespace arc
{
    /// Imports a glTF 2.0 model, either JSON (.gltf, with its buffers and images in separate files
    /// or embedded as data URIs) or binary (.glb). Returns nothing and logs the reason if the file
    /// cannot be used at all; parts it cannot use (unsupported primitive modes, broken accessors,
    /// undecodable images) are skipped with a warning and the rest is imported.
    ///
    /// What is converted:
    ///   - Every mesh primitive drawn as triangles (lists, strips and fans; strips and fans become
    ///     lists). Points and lines are skipped.
    ///   - POSITION, NORMAL and TEXCOORD_0, from any component type glTF allows (including the
    ///     normalized integers of KHR_mesh_quantization), with or without a byte stride, sparse or
    ///     not. Primitives without normals get flat normals, as the glTF specification requires.
    ///   - 8, 16 and 32-bit indices, all widened to 32 bits.
    ///   - The default scene's node hierarchy, flattened into mesh instances with complete transforms.
    ///   - Materials: base colour factor and texture, metallic and roughness factors, alpha mode and
    ///     cutoff, double-sidedness. Samplers.
    ///
    /// glTF's conventions match the engine's (right-handed, +Y up, metres, counter-clockwise front
    /// faces, column-major matrices), so nothing is flipped or converted.
    [[nodiscard]] std::optional<ModelData> importGltf(const std::string& path);

    /// The same, from a file already in memory. Relative URIs are resolved against `baseDirectory`.
    [[nodiscard]] std::optional<ModelData> importGltf(std::span<const std::byte> file, const std::string& baseDirectory,
                                                      std::string_view name);
}

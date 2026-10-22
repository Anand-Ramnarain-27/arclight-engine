#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace arc::test
{
    /// A 32-bit BMP, which stb_image decodes, written by hand so the tests need no encoder.
    [[nodiscard]] std::vector<std::byte> makeBmp(uint32_t width, uint32_t height, uint8_t red);

    /// One triangle as glTF, its buffer embedded as a data URI.
    [[nodiscard]] std::string triangleGltf();
}

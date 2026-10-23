#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>

namespace arc
{
    inline constexpr uint64_t kFnv1aBasis = 14695981039346656037ull;

    /// 64-bit FNV-1a. Fast and simple, for change detection and stable identifiers; not
    /// cryptographic, so never for anything an attacker could choose to collide. Pass a previous
    /// result as `hash` to continue it over more data.
    [[nodiscard]] constexpr uint64_t fnv1a64(std::span<const std::byte> data, uint64_t hash = kFnv1aBasis)
    {
        for (const std::byte byte : data)
        {
            hash ^= static_cast<uint64_t>(byte);
            hash *= 1099511628211ull;
        }
        return hash;
    }

    [[nodiscard]] constexpr uint64_t fnv1a64(std::string_view text, uint64_t hash = kFnv1aBasis)
    {
        for (const char c : text)
        {
            hash ^= static_cast<uint64_t>(static_cast<unsigned char>(c));
            hash *= 1099511628211ull;
        }
        return hash;
    }
}

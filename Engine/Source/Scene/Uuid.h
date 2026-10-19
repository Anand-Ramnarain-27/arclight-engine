#pragma once

#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <string_view>

namespace arc
{
    /// A 64-bit random identifier that stays the same across saves and loads, unlike a Handle, which
    /// is only valid while the program runs. Zero is the null UUID and is never generated.
    struct Uuid
    {
        uint64_t value = 0;

        /// Thread-safe.
        [[nodiscard]] static Uuid generate();

        /// Sixteen lowercase hexadecimal digits.
        [[nodiscard]] std::string toString() const;

        /// Accepts exactly what toString() produces, in either case.
        [[nodiscard]] static std::optional<Uuid> fromString(std::string_view text);

        [[nodiscard]] constexpr bool isNull() const { return value == 0; }
        constexpr explicit operator bool() const { return !isNull(); }
        constexpr auto operator<=>(const Uuid&) const = default;
    };
}

template <>
struct std::hash<arc::Uuid>
{
    size_t operator()(const arc::Uuid& uuid) const noexcept { return std::hash<uint64_t>{}(uuid.value); }
};

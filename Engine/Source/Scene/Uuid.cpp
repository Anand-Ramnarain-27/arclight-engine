#include "Scene/Uuid.h"

#include <charconv>
#include <format>
#include <random>

namespace arc
{
    Uuid Uuid::generate()
    {
        // One engine per thread, seeded once from the OS: no locking, and no two threads share a
        // sequence.
        thread_local std::mt19937_64 engine{[] {
            std::random_device device;
            return (uint64_t{device()} << 32) ^ uint64_t{device()};
        }()};

        uint64_t value = 0;
        while (value == 0)
            value = engine();
        return {value};
    }

    std::string Uuid::toString() const
    {
        return std::format("{:016x}", value);
    }

    std::optional<Uuid> Uuid::fromString(std::string_view text)
    {
        if (text.size() != 16)
            return std::nullopt;
        uint64_t value = 0;
        const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), value, 16);
        if (error != std::errc{} || end != text.data() + text.size())
            return std::nullopt;
        return Uuid{value};
    }
}

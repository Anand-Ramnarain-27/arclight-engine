#pragma once

#include <cstdint>
#include <map>
#include <optional>

namespace arc
{
    /// Hands out ranges of a fixed span of units (bytes, vertices, indices) from a list of free
    /// ranges, first fit. Freed ranges merge with free neighbours, so the space they leave can hold
    /// a range as large as all of them together. Not thread-safe.
    class RangeAllocator
    {
    public:
        explicit RangeAllocator(uint32_t capacity);

        /// The offset of a new range of `size` units, or nothing if no free range is that large.
        /// `size` must not be zero.
        [[nodiscard]] std::optional<uint32_t> allocate(uint32_t size);

        /// Returns a range that allocate() handed out. Freeing a range twice, or one that overlaps
        /// free space, asserts.
        void free(uint32_t offset, uint32_t size);

        [[nodiscard]] uint32_t capacity() const { return m_capacity; }
        [[nodiscard]] uint32_t used() const { return m_used; }
        [[nodiscard]] uint32_t largestFreeRange() const;
        /// More free ranges for the same free space means more fragmentation.
        [[nodiscard]] size_t freeRangeCount() const { return m_free.size(); }

    private:
        std::map<uint32_t, uint32_t> m_free;   // Offset to size, sorted by offset, never adjacent.
        uint32_t m_capacity;
        uint32_t m_used = 0;
    };
}

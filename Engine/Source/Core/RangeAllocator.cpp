#include "Core/RangeAllocator.h"

#include "Core/Assert.h"

#include <algorithm>
#include <iterator>

namespace arc
{
    RangeAllocator::RangeAllocator(uint32_t capacity) : m_capacity(capacity)
    {
        if (capacity > 0)
            m_free.emplace(0u, capacity);
    }

    std::optional<uint32_t> RangeAllocator::allocate(uint32_t size)
    {
        ARC_ASSERT(size > 0);
        for (auto it = m_free.begin(); it != m_free.end(); ++it)
        {
            if (it->second < size)
                continue;
            const uint32_t offset = it->first;
            const uint32_t remaining = it->second - size;
            m_free.erase(it);
            // The front of the free range is taken, so the rest keeps its place in the map order.
            if (remaining > 0)
                m_free.emplace(offset + size, remaining);
            m_used += size;
            return offset;
        }
        return std::nullopt;
    }

    void RangeAllocator::free(uint32_t offset, uint32_t size)
    {
        ARC_ASSERT(size > 0 && uint64_t{offset} + size <= m_capacity);
        auto next = m_free.lower_bound(offset);
        ARC_ASSERT(next == m_free.end() || offset + size <= next->first, "Freed range overlaps free space");

        uint32_t start = offset;
        uint32_t length = size;
        if (next != m_free.begin())
        {
            const auto previous = std::prev(next);
            ARC_ASSERT(previous->first + previous->second <= offset, "Freed range overlaps free space");
            if (previous->first + previous->second == offset)
            {
                start = previous->first;
                length += previous->second;
                m_free.erase(previous);
            }
        }
        if (next != m_free.end() && offset + size == next->first)
        {
            length += next->second;
            m_free.erase(next);
        }
        m_free.emplace(start, length);
        m_used -= size;
    }

    uint32_t RangeAllocator::largestFreeRange() const
    {
        uint32_t largest = 0;
        for (const auto& [offset, size] : m_free)
            largest = std::max(largest, size);
        return largest;
    }
}

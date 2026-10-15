#pragma once

#include <cstdint>
#include <limits>
#include <optional>
#include <utility>
#include <vector>

namespace arc
{
    /// A reference to an object owned elsewhere: a slot index plus the slot's generation when the
    /// handle was made. Freeing the slot bumps its generation, so every handle still pointing at it
    /// becomes stale and is detected instead of silently reaching whatever reuses the slot.
    ///
    /// `Tag` only makes handles to different kinds of object distinct types. A default-constructed
    /// handle is null: generation 0 is never handed out.
    template <typename Tag>
    struct Handle
    {
        uint32_t index = 0;
        uint32_t generation = 0;

        [[nodiscard]] constexpr bool isNull() const { return generation == 0; }
        constexpr explicit operator bool() const { return !isNull(); }
        constexpr bool operator==(const Handle&) const = default;
    };

    /// Hands out handles from a free list of slots, with a generation counter per slot.
    /// Not thread-safe.
    template <typename Tag>
    class HandleAllocator
    {
    public:
        using HandleType = Handle<Tag>;

        /// At most `capacity` slots are ever created.
        explicit HandleAllocator(uint32_t capacity = std::numeric_limits<uint32_t>::max()) : m_capacity(capacity) {}

        /// Returns nothing when every slot is in use.
        [[nodiscard]] std::optional<HandleType> allocate()
        {
            uint32_t index = 0;
            if (!m_freeList.empty())
            {
                index = m_freeList.back();
                m_freeList.pop_back();
            }
            else if (m_generations.size() < m_capacity)
            {
                index = static_cast<uint32_t>(m_generations.size());
                m_generations.push_back(1);
                m_alive.push_back(false);
            }
            else
            {
                return std::nullopt;
            }

            m_alive[index] = true;
            ++m_aliveCount;
            return HandleType{index, m_generations[index]};
        }

        /// Returns false, and changes nothing, if the handle is null or stale.
        bool free(HandleType handle)
        {
            if (!isValid(handle))
                return false;

            m_alive[handle.index] = false;
            --m_aliveCount;
            // A slot whose generation would wrap around is retired rather than reused: a wrapped
            // generation could make a very old handle look valid again.
            if (++m_generations[handle.index] != 0)
                m_freeList.push_back(handle.index);
            return true;
        }

        [[nodiscard]] bool isValid(HandleType handle) const
        {
            return !handle.isNull() && handle.index < m_generations.size() && m_alive[handle.index] &&
                   m_generations[handle.index] == handle.generation;
        }

        /// The live handle for a slot, for callers that only kept the index. Null if the slot is free.
        [[nodiscard]] HandleType handleAt(uint32_t index) const
        {
            if (index >= m_generations.size() || !m_alive[index])
                return {};
            return {index, m_generations[index]};
        }

        [[nodiscard]] uint32_t aliveCount() const { return m_aliveCount; }
        [[nodiscard]] uint32_t capacity() const { return m_capacity; }

        /// Slots created so far, live or free. Every live index is below this.
        [[nodiscard]] uint32_t slotCount() const { return static_cast<uint32_t>(m_generations.size()); }

    private:
        std::vector<uint32_t> m_generations;
        std::vector<bool> m_alive;
        std::vector<uint32_t> m_freeList;
        uint32_t m_capacity;
        uint32_t m_aliveCount = 0;
    };

    /// A HandleAllocator that also stores a value in each slot. get() returns nullptr for a null
    /// or stale handle. Not thread-safe.
    template <typename T, typename Tag = T>
    class HandlePool
    {
    public:
        using HandleType = Handle<Tag>;

        explicit HandlePool(uint32_t capacity = std::numeric_limits<uint32_t>::max()) : m_allocator(capacity) {}

        /// Returns a null handle when the pool is full.
        [[nodiscard]] HandleType insert(T value)
        {
            const std::optional<HandleType> handle = m_allocator.allocate();
            if (!handle)
                return {};
            if (handle->index >= m_values.size())
                m_values.resize(handle->index + 1);
            m_values[handle->index] = std::move(value);
            return *handle;
        }

        /// Returns false, and changes nothing, if the handle is null or stale.
        bool remove(HandleType handle)
        {
            if (!m_allocator.free(handle))
                return false;
            m_values[handle.index] = T{};
            return true;
        }

        [[nodiscard]] T* get(HandleType handle)
        {
            return m_allocator.isValid(handle) ? &m_values[handle.index] : nullptr;
        }

        [[nodiscard]] const T* get(HandleType handle) const
        {
            return m_allocator.isValid(handle) ? &m_values[handle.index] : nullptr;
        }

        [[nodiscard]] bool contains(HandleType handle) const { return m_allocator.isValid(handle); }
        [[nodiscard]] uint32_t size() const { return m_allocator.aliveCount(); }

        /// Calls `function(handle, value)` for every live entry, in slot order.
        template <typename Function>
        void forEach(Function&& function) const
        {
            for (uint32_t index = 0; index < m_allocator.slotCount(); ++index)
                if (const HandleType handle = m_allocator.handleAt(index))
                    function(handle, m_values[index]);
        }

    private:
        HandleAllocator<Tag> m_allocator;
        std::vector<T> m_values;
    };
}

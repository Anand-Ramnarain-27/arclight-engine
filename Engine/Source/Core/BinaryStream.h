#pragma once

#include <bit>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <span>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

namespace arc
{
    // Values are written in the machine's byte order. Every platform Arclight targets is
    // little-endian, so the files are too.
    static_assert(std::endian::native == std::endian::little, "Binary files assume a little-endian machine");

    /// Appends values to a byte buffer. Arrays and strings are written as a 32-bit count followed
    /// by their elements.
    class BinaryWriter
    {
    public:
        template <typename T>
            requires std::is_trivially_copyable_v<T>
        void write(const T& value)
        {
            const size_t offset = m_bytes.size();
            m_bytes.resize(offset + sizeof(T));
            std::memcpy(m_bytes.data() + offset, &value, sizeof(T));
        }

        void writeBytes(std::span<const std::byte> bytes) { m_bytes.insert(m_bytes.end(), bytes.begin(), bytes.end()); }

        void writeString(std::string_view text)
        {
            write(static_cast<uint32_t>(text.size()));
            writeBytes(std::as_bytes(std::span(text.data(), text.size())));
        }

        template <typename T>
            requires std::is_trivially_copyable_v<T>
        void writeArray(std::span<const T> values)
        {
            write(static_cast<uint32_t>(values.size()));
            writeBytes(std::as_bytes(values));
        }

        [[nodiscard]] const std::vector<std::byte>& bytes() const { return m_bytes; }
        [[nodiscard]] std::vector<std::byte> take() { return std::move(m_bytes); }

    private:
        std::vector<std::byte> m_bytes;
    };

    /// Reads what BinaryWriter wrote, checking every read against the end of the data. After the
    /// first read that runs past the end, or finds a count above its limit, the reader has failed:
    /// that and every later read return false and leave their output unchanged.
    class BinaryReader
    {
    public:
        explicit BinaryReader(std::span<const std::byte> bytes) : m_bytes(bytes) {}

        template <typename T>
            requires std::is_trivially_copyable_v<T>
        bool read(T& value)
        {
            if (!canRead(sizeof(T)))
                return false;
            std::memcpy(&value, m_bytes.data() + m_offset, sizeof(T));
            m_offset += sizeof(T);
            return true;
        }

        bool readString(std::string& text, uint32_t maxLength = 1u << 20)
        {
            uint32_t length = 0;
            if (!read(length) || !checkCount(length, maxLength, 1))
                return false;
            text.assign(reinterpret_cast<const char*>(m_bytes.data() + m_offset), length);
            m_offset += length;
            return true;
        }

        /// `maxCount` bounds the allocation a corrupt count could cause.
        template <typename T>
            requires std::is_trivially_copyable_v<T>
        bool readArray(std::vector<T>& values, uint32_t maxCount)
        {
            uint32_t count = 0;
            if (!read(count) || !checkCount(count, maxCount, sizeof(T)))
                return false;
            values.resize(count);
            if (count > 0)
                std::memcpy(values.data(), m_bytes.data() + m_offset, size_t{count} * sizeof(T));
            m_offset += size_t{count} * sizeof(T);
            return true;
        }

        /// A count followed by that many elements, each read by `readElement(reader, element)`.
        template <typename T, typename ReadElement>
        bool readEach(std::vector<T>& values, uint32_t maxCount, ReadElement&& readElement)
        {
            uint32_t count = 0;
            if (!read(count) || count > maxCount)
                return fail();
            values.clear();
            values.resize(count);
            for (T& value : values)
                if (!readElement(*this, value))
                    return fail();
            return !m_failed;
        }

        [[nodiscard]] bool failed() const { return m_failed; }
        [[nodiscard]] bool atEnd() const { return m_offset == m_bytes.size(); }
        [[nodiscard]] size_t remaining() const { return m_bytes.size() - m_offset; }

    private:
        bool fail()
        {
            m_failed = true;
            return false;
        }

        bool canRead(size_t size)
        {
            if (m_failed || size > remaining())
                return fail();
            return true;
        }

        bool checkCount(uint32_t count, uint32_t maxCount, size_t elementSize)
        {
            if (count > maxCount)
                return fail();
            return canRead(size_t{count} * elementSize);
        }

        std::span<const std::byte> m_bytes;
        size_t m_offset = 0;
        bool m_failed = false;
    };
}

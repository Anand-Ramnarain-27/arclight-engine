#pragma once

#include "RHI/RHITypes.h"

#include <cstddef>
#include <span>
#include <vector>

namespace arc::rhi
{
    class Buffer;
    class Device;

    /// Fills part of a GPU-only buffer through a temporary Upload buffer. Blocks until the copy
    /// has completed. Returns false and logs on failure.
    [[nodiscard]] bool uploadBuffer(Device& device, Buffer& dst, std::span<const std::byte> data, uint64_t dstOffset = 0);

    /// Copies part of a GPU-only buffer into a temporary Readback buffer and returns the bytes.
    /// Blocks until the copy has completed. Returns an empty vector and logs on failure.
    [[nodiscard]] std::vector<std::byte> readbackBuffer(Device& device, Buffer& src, uint64_t srcOffset, uint64_t size);

    /// Uploads a test pattern to a GPU-only buffer, reads it back and compares. Exercises all
    /// three memory types, copies and barriers. Returns true if the bytes match.
    [[nodiscard]] bool verifyBufferRoundTrip(Device& device, uint64_t size = 64 * 1024);
}

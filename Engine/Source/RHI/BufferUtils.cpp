#include "RHI/BufferUtils.h"

#include "Core/Assert.h"
#include "Core/Log.h"
#include "RHI/Buffer.h"
#include "RHI/CommandList.h"
#include "RHI/Device.h"

#include <cstring>

namespace arc::rhi
{
    bool uploadBuffer(Device& device, Buffer& dst, std::span<const std::byte> data, uint64_t dstOffset)
    {
        ARC_ASSERT(dst.desc().memory == MemoryType::GpuOnly, "Upload and Readback buffers are written through mappedData()");
        ARC_ASSERT(dstOffset + data.size() <= dst.desc().size, "Upload past the end of '{}'", dst.desc().debugName);
        if (data.empty())
            return true;

        auto staging = device.createBuffer({
            .size = data.size(),
            .memory = MemoryType::Upload,
            .debugName = "Staging (" + dst.desc().debugName + ")",
        });
        if (!staging)
            return false;

        std::memcpy(staging->mappedData(), data.data(), data.size());

        device.submitImmediate([&](CommandList& commandList) {
            commandList.barrier(dst, ResourceState::CopyDest);
            commandList.copyBuffer(dst, dstOffset, *staging, 0, data.size());
        });

        // submitImmediate has waited for the GPU, so the staging buffer can be destroyed here.
        return true;
    }

    std::vector<std::byte> readbackBuffer(Device& device, Buffer& src, uint64_t srcOffset, uint64_t size)
    {
        ARC_ASSERT(src.desc().memory == MemoryType::GpuOnly);
        ARC_ASSERT(srcOffset + size <= src.desc().size, "Readback past the end of '{}'", src.desc().debugName);

        auto readback = device.createBuffer({
            .size = size,
            .memory = MemoryType::Readback,
            .debugName = "Readback (" + src.desc().debugName + ")",
        });
        if (!readback)
            return {};

        device.submitImmediate([&](CommandList& commandList) {
            commandList.barrier(src, ResourceState::CopySource);
            commandList.copyBuffer(*readback, 0, src, srcOffset, size);
        });

        std::vector<std::byte> bytes(size);
        std::memcpy(bytes.data(), readback->mappedData(), size);
        return bytes;
    }

    bool verifyBufferRoundTrip(Device& device, uint64_t size)
    {
        auto buffer = device.createBuffer({
            .size = size,
            .usage = BufferUsage::Vertex,
            .memory = MemoryType::GpuOnly,
            .debugName = "Round-trip test",
        });
        if (!buffer)
            return false;

        // A pattern that differs per byte position, so a wrong offset or a partial copy is caught.
        std::vector<std::byte> pattern(size);
        for (uint64_t i = 0; i < size; ++i)
            pattern[i] = static_cast<std::byte>((i * 31 + (i >> 8)) & 0xFF);

        if (!uploadBuffer(device, *buffer, pattern))
            return false;
        if (buffer->state() != ResourceState::Common)
        {
            ARC_LOG_ERROR("Buffer round-trip: expected state Common after submission, got {}",
                          resourceStateName(buffer->state()));
            return false;
        }

        const std::vector<std::byte> result = readbackBuffer(device, *buffer, 0, size);
        if (result != pattern)
        {
            ARC_LOG_ERROR("Buffer round-trip: {} bytes read back did not match what was uploaded", size);
            return false;
        }
        return true;
    }
}

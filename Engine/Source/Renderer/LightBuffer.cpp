#include "Renderer/LightBuffer.h"

#include "Core/Assert.h"
#include "RHI/Buffer.h"
#include "RHI/Device.h"

#include <algorithm>
#include <cstring>
#include <string>

namespace arc
{
    LightBuffer::LightBuffer() = default;

    // Defined here, where Buffer is complete, for the unique_ptr destructors.
    LightBuffer::~LightBuffer() = default;

    bool LightBuffer::init(rhi::Device& device)
    {
        for (uint32_t slot = 0; slot < rhi::kMaxFramesInFlight; ++slot)
        {
            m_buffers[slot] = device.createBuffer({
                .size = uint64_t{kMaxLights} * sizeof(GpuLight),
                .usage = rhi::BufferUsage::Structured,
                .stride = sizeof(GpuLight),
                .memory = rhi::MemoryType::Upload,
                .debugName = "Lights " + std::to_string(slot),
            });
            if (!m_buffers[slot])
                return false;
        }
        return true;
    }

    void LightBuffer::shutdown()
    {
        for (auto& buffer : m_buffers)
            buffer.reset();
    }

    uint32_t LightBuffer::write(uint32_t frameSlot, std::span<const GpuLight> lights)
    {
        ARC_ASSERT(frameSlot < rhi::kMaxFramesInFlight);
        rhi::Buffer* buffer = m_buffers[frameSlot].get();
        if (!buffer)
            return 0;
        const auto count = static_cast<uint32_t>(std::min<size_t>(lights.size(), kMaxLights));
        if (count > 0)
            std::memcpy(buffer->mappedData(), lights.data(), count * sizeof(GpuLight));
        return count;
    }
}

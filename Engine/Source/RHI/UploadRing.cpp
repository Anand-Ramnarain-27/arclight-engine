#include "RHI/UploadRing.h"

#include "Core/Assert.h"
#include "Core/Log.h"
#include "RHI/Buffer.h"
#include "RHI/Device.h"

#include <cstring>
#include <string>

namespace arc::rhi
{
    namespace
    {
        constexpr uint64_t kRegionCount = kMaxFramesInFlight + 1;
        constexpr uint64_t kZeroBlockSize = kMaxConstantBufferSize;

        constexpr uint64_t alignUp(uint64_t value, uint64_t alignment)
        {
            return (value + alignment - 1) / alignment * alignment;
        }
    }

    UploadRing::UploadRing() = default;

    // Defined here, where Buffer is complete, for the unique_ptr destructor.
    UploadRing::~UploadRing() = default;

    bool UploadRing::init(Device& device, uint64_t regionSize, std::string_view debugName)
    {
        m_regionSize = alignUp(regionSize, kConstantBufferAlignment);
        m_used.assign(kRegionCount, 0);
        m_buffer = device.createBuffer({
            .size = kZeroBlockSize + m_regionSize * kRegionCount,
            .usage = BufferUsage::Constant,
            .memory = MemoryType::Upload,
            .debugName = std::string(debugName),
        });
        if (!m_buffer)
            return false;

        std::memset(m_buffer->mappedData(), 0, kZeroBlockSize);
        return true;
    }

    void UploadRing::reset(uint32_t region)
    {
        ARC_ASSERT(region < m_used.size());
        m_used[region] = 0;
    }

    UploadAllocation UploadRing::allocate(uint32_t region, uint64_t size, uint64_t alignment)
    {
        ARC_ASSERT(region < m_used.size());
        ARC_ASSERT(alignment > 0 && (alignment & (alignment - 1)) == 0, "Alignment {} is not a power of two", alignment);

        // Region starts are multiples of kConstantBufferAlignment, so aligning the offset within
        // the region aligns the absolute offset for any alignment up to that.
        ARC_ASSERT(alignment <= kConstantBufferAlignment);
        const uint64_t start = alignUp(m_used[region], alignment);
        if (start + size > m_regionSize)
        {
            if (!m_reportedOverflow)
                ARC_LOG_ERROR("Upload ring '{}' is full ({} bytes per frame); data is being dropped",
                              m_buffer->desc().debugName, m_regionSize);
            m_reportedOverflow = true;
            return {};
        }
        m_used[region] = start + size;

        const uint64_t offset = kZeroBlockSize + region * m_regionSize + start;
        return {m_buffer.get(), offset, static_cast<std::byte*>(m_buffer->mappedData()) + offset};
    }

    UploadAllocation UploadRing::zeroBlock() const
    {
        return {m_buffer.get(), 0, nullptr};
    }
}

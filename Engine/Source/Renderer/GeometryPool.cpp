#include "Renderer/GeometryPool.h"

#include "Core/Assert.h"
#include "Core/Log.h"
#include "RHI/Buffer.h"
#include "RHI/BufferUtils.h"
#include "RHI/Device.h"

#include <algorithm>
#include <format>

namespace arc
{
    GeometryPool::GeometryPool(rhi::Device& device, Desc desc) : m_device(device), m_desc(desc) {}

    GeometryPool::~GeometryPool()
    {
        if (m_liveAllocations > 0)
            ARC_LOG_ERROR("Geometry pool destroyed with {} allocations still alive", m_liveAllocations);
    }

    bool GeometryPool::addBlock(const Desc& capacities)
    {
        const auto index = m_blocks.size();
        const auto create = [&](uint64_t size, rhi::BufferUsage usage, std::string_view kind) {
            return m_device.createBuffer({
                .size = size,
                .usage = usage,
                .memory = rhi::MemoryType::GpuOnly,
                .debugName = std::format("Geometry Pool {} {}", index, kind),
            });
        };
        Block block{
            .vertices = create(uint64_t{capacities.vertexCapacity} * sizeof(MeshVertex), rhi::BufferUsage::Vertex, "Vertices"),
            .indices16 = create(uint64_t{capacities.index16Capacity} * sizeof(uint16_t), rhi::BufferUsage::Index, "Indices16"),
            .indices32 = create(uint64_t{capacities.index32Capacity} * sizeof(uint32_t), rhi::BufferUsage::Index, "Indices32"),
            .vertexRanges = RangeAllocator(capacities.vertexCapacity),
            .index16Ranges = RangeAllocator(capacities.index16Capacity),
            .index32Ranges = RangeAllocator(capacities.index32Capacity),
        };
        if (!block.vertices || !block.indices16 || !block.indices32)
        {
            ARC_LOG_ERROR("Could not create geometry pool block {}", index);
            return false;
        }
        m_blocks.push_back(std::move(block));
        return true;
    }

    bool GeometryPool::tryAllocate(Block& block, uint32_t vertexCount, uint32_t index16Count, uint32_t index32Count,
                                   GeometryAllocation& allocation)
    {
        // All three or none: a partial allocation is undone before trying the next block.
        std::optional<uint32_t> vertices = vertexCount > 0 ? block.vertexRanges.allocate(vertexCount) : 0u;
        std::optional<uint32_t> indices16 = index16Count > 0 ? block.index16Ranges.allocate(index16Count) : 0u;
        std::optional<uint32_t> indices32 = index32Count > 0 ? block.index32Ranges.allocate(index32Count) : 0u;
        if (vertices && indices16 && indices32)
        {
            allocation.vertices = {*vertices, vertexCount};
            allocation.indices16 = {*indices16, index16Count};
            allocation.indices32 = {*indices32, index32Count};
            return true;
        }
        if (vertices && vertexCount > 0)
            block.vertexRanges.free(*vertices, vertexCount);
        if (indices16 && index16Count > 0)
            block.index16Ranges.free(*indices16, index16Count);
        if (indices32 && index32Count > 0)
            block.index32Ranges.free(*indices32, index32Count);
        return false;
    }

    std::optional<GeometryAllocation> GeometryPool::allocate(uint32_t vertexCount, uint32_t index16Count,
                                                             uint32_t index32Count, std::string_view debugName)
    {
        GeometryAllocation allocation;
        for (uint32_t i = 0; i < m_blocks.size(); ++i)
        {
            if (tryAllocate(m_blocks[i], vertexCount, index16Count, index32Count, allocation))
            {
                allocation.block = i;
                ++m_liveAllocations;
                return allocation;
            }
        }

        // A model larger than the default block gets a block of its own size.
        const Desc capacities{
            .vertexCapacity = std::max(m_desc.vertexCapacity, vertexCount),
            .index16Capacity = std::max(m_desc.index16Capacity, index16Count),
            .index32Capacity = std::max(m_desc.index32Capacity, index32Count),
        };
        if (!addBlock(capacities))
            return std::nullopt;
        ARC_LOG_TRACE("Geometry pool: block {} added for '{}'", m_blocks.size() - 1, debugName);
        const bool fits = tryAllocate(m_blocks.back(), vertexCount, index16Count, index32Count, allocation);
        ARC_ASSERT(fits);
        allocation.block = static_cast<uint32_t>(m_blocks.size() - 1);
        ++m_liveAllocations;
        return allocation;
    }

    bool GeometryPool::upload(const GeometryAllocation& allocation, std::span<const MeshVertex> vertices,
                              std::span<const uint16_t> indices16, std::span<const uint32_t> indices32)
    {
        ARC_ASSERT(allocation.isValid() && allocation.block < m_blocks.size());
        ARC_ASSERT(vertices.size() == allocation.vertices.count && indices16.size() == allocation.indices16.count &&
                   indices32.size() == allocation.indices32.count);
        const Block& block = m_blocks[allocation.block];
        const auto copy = [this](rhi::Buffer& buffer, std::span<const std::byte> bytes, uint64_t offset) {
            return bytes.empty() || rhi::uploadBuffer(m_device, buffer, bytes, offset);
        };
        return copy(*block.vertices, std::as_bytes(vertices), uint64_t{allocation.vertices.offset} * sizeof(MeshVertex)) &&
               copy(*block.indices16, std::as_bytes(indices16), uint64_t{allocation.indices16.offset} * sizeof(uint16_t)) &&
               copy(*block.indices32, std::as_bytes(indices32), uint64_t{allocation.indices32.offset} * sizeof(uint32_t));
    }

    void GeometryPool::free(const GeometryAllocation& allocation)
    {
        if (!allocation.isValid())
            return;
        ARC_ASSERT(allocation.block < m_blocks.size());
        m_pendingFrees.push_back({allocation, m_device.frameNumber()});
        --m_liveAllocations;
    }

    void GeometryPool::release(const GeometryAllocation& allocation)
    {
        Block& block = m_blocks[allocation.block];
        if (allocation.vertices.count > 0)
            block.vertexRanges.free(allocation.vertices.offset, allocation.vertices.count);
        if (allocation.indices16.count > 0)
            block.index16Ranges.free(allocation.indices16.offset, allocation.indices16.count);
        if (allocation.indices32.count > 0)
            block.index32Ranges.free(allocation.indices32.offset, allocation.indices32.count);
    }

    void GeometryPool::collect(uint64_t frameNumber)
    {
        // A range freed at frame number T may be read by the frame being recorded then, frame T.
        // Beginning frame T + kMaxFramesInFlight waits for frame T's slot, so by then it is done.
        std::erase_if(m_pendingFrees, [&](const PendingFree& pending) {
            if (frameNumber < pending.frameNumber + rhi::kMaxFramesInFlight)
                return false;
            release(pending.allocation);
            return true;
        });
    }

    rhi::Buffer& GeometryPool::vertexBuffer(uint32_t block) const
    {
        return *m_blocks[block].vertices;
    }

    rhi::Buffer& GeometryPool::indexBuffer(uint32_t block, rhi::IndexFormat format) const
    {
        return format == rhi::IndexFormat::UInt16 ? *m_blocks[block].indices16 : *m_blocks[block].indices32;
    }

    GeometryPool::Stats GeometryPool::stats() const
    {
        Stats stats{.blocks = static_cast<uint32_t>(m_blocks.size()),
                    .allocations = m_liveAllocations,
                    .pendingFrees = static_cast<uint32_t>(m_pendingFrees.size())};
        for (const Block& block : m_blocks)
        {
            stats.vertexBytesUsed += uint64_t{block.vertexRanges.used()} * sizeof(MeshVertex);
            stats.vertexBytesCapacity += uint64_t{block.vertexRanges.capacity()} * sizeof(MeshVertex);
            stats.indexBytesUsed += uint64_t{block.index16Ranges.used()} * sizeof(uint16_t) +
                                    uint64_t{block.index32Ranges.used()} * sizeof(uint32_t);
            stats.indexBytesCapacity += uint64_t{block.index16Ranges.capacity()} * sizeof(uint16_t) +
                                        uint64_t{block.index32Ranges.capacity()} * sizeof(uint32_t);
        }
        return stats;
    }
}

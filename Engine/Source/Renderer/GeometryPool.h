#pragma once

#include "Core/RangeAllocator.h"
#include "RHI/RHITypes.h"
#include "Renderer/ModelData.h"

#include <cstdint>
#include <limits>
#include <memory>
#include <optional>
#include <span>
#include <string_view>
#include <vector>

namespace arc
{
    namespace rhi
    {
        class Buffer;
        class Device;
    }

    /// A run of elements in one of a GeometryPool block's buffers: vertices, or indices of one size.
    struct GeometryRange
    {
        uint32_t offset = 0;   // In elements, not bytes.
        uint32_t count = 0;
    };

    /// Where a model's geometry lives in the pool. All three ranges are in the same block, so the
    /// model draws from one vertex buffer and at most two index buffers.
    struct GeometryAllocation
    {
        static constexpr uint32_t kNoBlock = std::numeric_limits<uint32_t>::max();

        uint32_t block = kNoBlock;
        GeometryRange vertices;
        GeometryRange indices16;
        GeometryRange indices32;

        [[nodiscard]] bool isValid() const { return block != kNoBlock; }
    };

    /// Static geometry for every model in a few large GPU buffers instead of a vertex and an index
    /// buffer per model, so drawing many models needs few buffer binds and creating one makes no
    /// new GPU allocations.
    ///
    /// The pool is a list of blocks; each holds a vertex buffer of MeshVertex and two index buffers,
    /// one of 16-bit and one of 32-bit indices, and suballocates them with a RangeAllocator. A model
    /// that fits in no block gets a new one, at least large enough for it.
    ///
    /// Freed ranges are reused only once no frame in flight can still be reading them: free() tags
    /// them with the device's frame number, and collect() returns them to the allocators after
    /// kMaxFramesInFlight more frames have begun.
    class GeometryPool
    {
    public:
        struct Desc
        {
            uint32_t vertexCapacity = 1u << 20;    // Per block. 32 MB of MeshVertex.
            uint32_t index16Capacity = 4u << 20;   // 8 MB.
            uint32_t index32Capacity = 2u << 20;   // 8 MB.
        };

        struct Stats
        {
            uint32_t blocks = 0;
            uint64_t vertexBytesUsed = 0;
            uint64_t vertexBytesCapacity = 0;
            uint64_t indexBytesUsed = 0;
            uint64_t indexBytesCapacity = 0;
            uint32_t allocations = 0;
            uint32_t pendingFrees = 0;
        };

        explicit GeometryPool(rhi::Device& device) : GeometryPool(device, Desc{}) {}
        GeometryPool(rhi::Device& device, Desc desc);
        /// Reports allocations still alive. Must be destroyed before the device.
        ~GeometryPool();

        GeometryPool(const GeometryPool&) = delete;
        GeometryPool& operator=(const GeometryPool&) = delete;

        /// Reserves ranges for the given numbers of vertices and indices, any of which may be zero.
        /// Returns nothing, after logging, if a new block was needed and could not be created.
        [[nodiscard]] std::optional<GeometryAllocation> allocate(uint32_t vertexCount, uint32_t index16Count,
                                                                 uint32_t index32Count, std::string_view debugName);

        /// Copies data into the allocation's ranges; each span must match its range's count. Blocks
        /// until the copies are done. Returns false and logs on failure.
        bool upload(const GeometryAllocation& allocation, std::span<const MeshVertex> vertices,
                    std::span<const uint16_t> indices16, std::span<const uint32_t> indices32);

        /// The ranges become reusable once frames in flight that may draw from them have finished.
        void free(const GeometryAllocation& allocation);

        /// Reuses the ranges whose frames have finished. `frameNumber` is the device's: the number
        /// of frames submitted so far. Called by the Renderer at the start of every frame.
        void collect(uint64_t frameNumber);

        [[nodiscard]] rhi::Buffer& vertexBuffer(uint32_t block) const;
        [[nodiscard]] rhi::Buffer& indexBuffer(uint32_t block, rhi::IndexFormat format) const;
        [[nodiscard]] rhi::Device& device() const { return m_device; }
        [[nodiscard]] Stats stats() const;

    private:
        struct Block
        {
            std::unique_ptr<rhi::Buffer> vertices;
            std::unique_ptr<rhi::Buffer> indices16;
            std::unique_ptr<rhi::Buffer> indices32;
            RangeAllocator vertexRanges;
            RangeAllocator index16Ranges;
            RangeAllocator index32Ranges;
        };

        struct PendingFree
        {
            GeometryAllocation allocation;
            uint64_t frameNumber = 0;
        };

        [[nodiscard]] bool tryAllocate(Block& block, uint32_t vertexCount, uint32_t index16Count, uint32_t index32Count,
                                       GeometryAllocation& allocation);
        [[nodiscard]] bool addBlock(const Desc& capacities);
        void release(const GeometryAllocation& allocation);

        rhi::Device& m_device;
        Desc m_desc;
        std::vector<Block> m_blocks;
        std::vector<PendingFree> m_pendingFrees;
        uint32_t m_liveAllocations = 0;
    };
}

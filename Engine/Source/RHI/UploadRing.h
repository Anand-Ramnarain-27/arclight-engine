#pragma once

#include "RHI/RHITypes.h"

#include <cstddef>
#include <memory>
#include <string_view>
#include <vector>

namespace arc::rhi
{
    class Buffer;
    class Device;

    /// A piece of an UploadRing: CPU-writable memory the GPU reads at `buffer` + `offset`.
    struct UploadAllocation
    {
        Buffer* buffer = nullptr;
        uint64_t offset = 0;      // Bytes.
        std::byte* cpu = nullptr; // Write-combined: write it sequentially, never read it back.

        [[nodiscard]] explicit operator bool() const { return buffer != nullptr; }
    };

    /// Per-frame scratch memory for data the CPU writes every frame, such as constants.
    ///
    /// One Upload buffer split into regions: one per frame slot, plus one for immediate
    /// submissions. A frame allocates linearly from its own region, and the region is reset when
    /// that frame slot comes round again, after the device has waited for the GPU to finish it.
    /// So nothing the GPU may still be reading is ever overwritten, and allocating is a pointer bump.
    ///
    /// The buffer also starts with kMaxConstantBufferSize bytes of zeros that are never handed out,
    /// bound to constant slots nothing has set so that shaders read zero rather than garbage.
    class UploadRing
    {
    public:
        static constexpr uint32_t kImmediateRegion = kMaxFramesInFlight;

        UploadRing();
        ~UploadRing();

        UploadRing(const UploadRing&) = delete;
        UploadRing& operator=(const UploadRing&) = delete;

        /// `regionSize` is in bytes. Returns false and logs on failure.
        [[nodiscard]] bool init(Device& device, uint64_t regionSize, std::string_view debugName);

        /// The GPU must have finished every command list that allocated from the region.
        void reset(uint32_t region);

        /// Returns an empty allocation and logs if the region is full.
        [[nodiscard]] UploadAllocation allocate(uint32_t region, uint64_t size, uint64_t alignment);

        /// kMaxConstantBufferSize bytes of zeros.
        [[nodiscard]] UploadAllocation zeroBlock() const;

        [[nodiscard]] uint64_t used(uint32_t region) const { return m_used[region]; }
        [[nodiscard]] uint64_t regionSize() const { return m_regionSize; }

    private:
        std::unique_ptr<Buffer> m_buffer;
        uint64_t m_regionSize = 0;
        std::vector<uint64_t> m_used;   // Per region, in bytes.
        bool m_reportedOverflow = false;
    };
}

#pragma once

#include "RHI/RHITypes.h"

namespace arc::rhi
{
    /// A linear block of GPU-visible memory. Created by Device::createBuffer().
    class Buffer
    {
    public:
        virtual ~Buffer() = default;

        Buffer(const Buffer&) = delete;
        Buffer& operator=(const Buffer&) = delete;

        [[nodiscard]] const BufferDesc& desc() const { return m_desc; }

        /// CPU pointer to the buffer's memory for Upload and Readback buffers; nullptr for
        /// GpuOnly. Valid for the buffer's lifetime.
        [[nodiscard]] void* mappedData() const { return m_mapped; }

        /// The state at the current point of command recording, set by CommandList::barrier().
        /// Returns to Common when the command list is submitted, matching D3D12, where buffers
        /// decay to COMMON when the GPU finishes each ExecuteCommandLists call.
        [[nodiscard]] ResourceState state() const { return m_state; }
        void setState(ResourceState state) { m_state = state; }

    protected:
        Buffer() = default;

        BufferDesc m_desc;
        void* m_mapped = nullptr;
        ResourceState m_state = ResourceState::Common;
    };
}

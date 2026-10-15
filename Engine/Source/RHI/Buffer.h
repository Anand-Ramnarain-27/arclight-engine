#pragma once

#include "RHI/RHITypes.h"

namespace arc::rhi
{
    class Device;

    /// A linear block of GPU-visible memory. Created by Device::createBuffer().
    class Buffer
    {
    public:
        /// The GPU may still be using the buffer: backends that need to keep it alive until then
        /// hand the native object to their deferred release queue.
        virtual ~Buffer();

        Buffer(const Buffer&) = delete;
        Buffer& operator=(const Buffer&) = delete;

        [[nodiscard]] const BufferDesc& desc() const { return m_desc; }

        /// Null for buffers not created through Device::createBuffer().
        [[nodiscard]] BufferHandle handle() const { return m_handle; }

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

    private:
        friend class Device;
        Device* m_owner = nullptr;
        BufferHandle m_handle;
    };
}

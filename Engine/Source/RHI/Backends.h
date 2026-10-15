#pragma once

#include "RHI/RHITypes.h"

#include <memory>

// Factory functions for each backend. Only the backends for the current platform are compiled,
// so only those factories are defined.
namespace arc::rhi
{
    class Device;

    [[nodiscard]] std::unique_ptr<Device> createD3D12Device(const DeviceDesc& desc);
    [[nodiscard]] std::unique_ptr<Device> createMetalDevice(const DeviceDesc& desc);
    [[nodiscard]] std::unique_ptr<Device> createNullDevice(const DeviceDesc& desc);
}

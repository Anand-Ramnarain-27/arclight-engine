#include "RHI/Device.h"

#include "RHI/Backends.h"

namespace arc::rhi
{
    const char* backendName(Backend backend)
    {
        switch (backend)
        {
            case Backend::D3D12: return "Direct3D 12";
            case Backend::Metal: return "Metal";
            case Backend::Null:  return "Null";
        }
        return "Unknown";
    }

    std::unique_ptr<Device> Device::create(const DeviceDesc& desc)
    {
#if ARC_PLATFORM_WINDOWS
        return createD3D12Device(desc);
#elif ARC_PLATFORM_MACOS
        return createMetalDevice(desc);
#else
        return createNullDevice(desc);
#endif
    }
}

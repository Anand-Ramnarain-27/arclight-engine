#include "RHI/ShaderUtils.h"

#include <cstring>

namespace arc::rhi
{
    bool looksLikeDxil(std::span<const std::byte> bytecode)
    {
        // DXIL ships inside the same container format as legacy DXBC bytecode.
        constexpr char kMagic[4] = {'D', 'X', 'B', 'C'};
        return bytecode.size() > sizeof(kMagic) && std::memcmp(bytecode.data(), kMagic, sizeof(kMagic)) == 0;
    }
}

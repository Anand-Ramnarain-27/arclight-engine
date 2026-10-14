#pragma once

#include <cstddef>
#include <span>

namespace arc::rhi
{
    /// True if the bytes start with the DXBC container header that DXC writes around DXIL.
    /// Catches a wrong or truncated file before the driver sees it.
    [[nodiscard]] bool looksLikeDxil(std::span<const std::byte> bytecode);
}

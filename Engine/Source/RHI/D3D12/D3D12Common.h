#pragma once

#include "RHI/RHITypes.h"

// Only D3D12 backend sources include this header, so <windows.h> stays out of engine headers.
#ifndef WIN32_LEAN_AND_MEAN
    #define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
    #define NOMINMAX
#endif
#include <windows.h>

#include <d3d12.h>
#include <dxgi1_6.h>
#include <wrl/client.h>

#include <source_location>
#include <string>
#include <string_view>

namespace arc::rhi::d3d12
{
    using Microsoft::WRL::ComPtr;

    /// Logs a failed HRESULT with the call that produced it. Returns true on success.
    [[nodiscard]] bool succeeded(HRESULT hr, std::string_view call,
                                 const std::source_location& where = std::source_location::current());

    [[nodiscard]] DXGI_FORMAT toDxgiFormat(Format format);

    [[nodiscard]] std::string narrow(const wchar_t* text);
    [[nodiscard]] std::wstring widen(std::string_view text);

    /// Names the object in debug layer messages, PIX and RenderDoc.
    void setDebugName(ID3D12Object* object, std::string_view name);
}

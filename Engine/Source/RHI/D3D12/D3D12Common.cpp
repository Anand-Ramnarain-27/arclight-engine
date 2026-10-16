#include "RHI/D3D12/D3D12Common.h"

#include "Core/Log.h"

namespace arc::rhi::d3d12
{
    bool succeeded(HRESULT hr, std::string_view call, const std::source_location& where)
    {
        if (SUCCEEDED(hr))
            return true;

        char* systemMessage = nullptr;
        FormatMessageA(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
                       nullptr, static_cast<DWORD>(hr), 0, reinterpret_cast<LPSTR>(&systemMessage), 0, nullptr);
        std::string_view description = systemMessage ? systemMessage : "unknown error";
        while (!description.empty() && (description.back() == '\n' || description.back() == '\r'))
            description.remove_suffix(1);

        log::write(log::Level::Error, where,
                   std::format("{} failed: 0x{:08X} ({})", call, static_cast<uint32_t>(hr), description));
        LocalFree(systemMessage);
        return false;
    }

    DXGI_FORMAT toDxgiFormat(Format format)
    {
        switch (format)
        {
            case Format::Unknown:          return DXGI_FORMAT_UNKNOWN;
            case Format::RGBA8_UNORM:      return DXGI_FORMAT_R8G8B8A8_UNORM;
            case Format::RGBA8_UNORM_SRGB: return DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
            case Format::BGRA8_UNORM:      return DXGI_FORMAT_B8G8R8A8_UNORM;
            case Format::D32_FLOAT:        return DXGI_FORMAT_D32_FLOAT;
        }
        return DXGI_FORMAT_UNKNOWN;
    }

    DXGI_FORMAT toDxgiFormat(VertexFormat format)
    {
        switch (format)
        {
            case VertexFormat::Float2: return DXGI_FORMAT_R32G32_FLOAT;
            case VertexFormat::Float3: return DXGI_FORMAT_R32G32B32_FLOAT;
            case VertexFormat::Float4: return DXGI_FORMAT_R32G32B32A32_FLOAT;
        }
        return DXGI_FORMAT_UNKNOWN;
    }

    std::string narrow(const wchar_t* text)
    {
        const int bytes = WideCharToMultiByte(CP_UTF8, 0, text, -1, nullptr, 0, nullptr, nullptr);
        if (bytes <= 1)
            return {};
        std::string result(static_cast<size_t>(bytes - 1), '\0');
        WideCharToMultiByte(CP_UTF8, 0, text, -1, result.data(), bytes, nullptr, nullptr);
        return result;
    }

    std::wstring widen(std::string_view text)
    {
        if (text.empty())
            return {};
        const int length = static_cast<int>(text.size());
        const int chars = MultiByteToWideChar(CP_UTF8, 0, text.data(), length, nullptr, 0);
        std::wstring result(static_cast<size_t>(chars), L'\0');
        MultiByteToWideChar(CP_UTF8, 0, text.data(), length, result.data(), chars);
        return result;
    }

    void setDebugName(ID3D12Object* object, std::string_view name)
    {
        if (object && !name.empty())
            object->SetName(widen(name).c_str());
    }
}

#include "Renderer/ShaderLibrary.h"

#include "Core/Log.h"
#include "RHI/Device.h"
#include "RHI/Shader.h"

#include <SDL3/SDL.h>

#include <cstddef>
#include <format>
#include <span>

namespace arc
{
    namespace
    {
        const char* stageSuffix(rhi::ShaderStage stage)
        {
            switch (stage)
            {
                case rhi::ShaderStage::Vertex: return "vs";
                case rhi::ShaderStage::Pixel:  return "ps";
            }
            return "unknown";
        }

        const char* formatExtension(rhi::ShaderFormat format)
        {
            switch (format)
            {
                case rhi::ShaderFormat::DXIL:     return "dxil";
                case rhi::ShaderFormat::MetalLib: return "metallib";
            }
            return "unknown";
        }
    }

    ShaderLibrary::ShaderLibrary(rhi::Device& device) : m_device(device)
    {
        // The executable's folder rather than the working directory, which the IDEs set to Game/.
        const char* basePath = SDL_GetBasePath();
        m_directory = std::string(basePath ? basePath : "") + "Shaders/";
    }

    std::unique_ptr<rhi::Shader> ShaderLibrary::load(std::string_view name, rhi::ShaderStage stage,
                                                     std::string_view entryPoint) const
    {
        const std::string fileName = std::format("{}.{}.{}", name, stageSuffix(stage), formatExtension(m_device.shaderFormat()));
        const std::string path = m_directory + fileName;

        // SDL_LoadFile handles UTF-8 paths on every platform, including Windows.
        size_t size = 0;
        void* data = SDL_LoadFile(path.c_str(), &size);
        if (!data)
        {
            ARC_LOG_ERROR("Cannot read shader '{}': {}", path, SDL_GetError());
            return nullptr;
        }

        rhi::ShaderDesc desc;
        desc.stage = stage;
        desc.bytecode = std::span(static_cast<const std::byte*>(data), size);
        desc.entryPoint = entryPoint;
        desc.debugName = fileName;

        auto shader = m_device.createShader(desc);
        SDL_free(data);
        return shader;
    }
}

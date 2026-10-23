#include "Renderer/ShaderLibrary.h"

#include "Core/Log.h"
#include "Platform/FileSystem.h"
#include "RHI/Device.h"
#include "RHI/Shader.h"

#include <cstddef>
#include <format>
#include <optional>
#include <vector>

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
        // The engine mount is the executable's folder, where the build puts the compiled shaders,
        // rather than the working directory, which the IDEs set to Game/.
        m_directory = "engine://Shaders/";
    }

    std::unique_ptr<rhi::Shader> ShaderLibrary::load(std::string_view name, rhi::ShaderStage stage,
                                                     std::string_view entryPoint) const
    {
        const std::string fileName = std::format("{}.{}.{}", name, stageSuffix(stage), formatExtension(m_device.shaderFormat()));
        const std::string path = m_directory + fileName;

        const std::optional<std::vector<std::byte>> bytecode = readFile(path);
        if (!bytecode)
        {
            ARC_LOG_ERROR("Cannot read shader '{}'", path);
            return nullptr;
        }

        rhi::ShaderDesc desc;
        desc.stage = stage;
        desc.bytecode = *bytecode;
        desc.entryPoint = entryPoint;
        desc.debugName = fileName;
        return m_device.createShader(desc);
    }
}

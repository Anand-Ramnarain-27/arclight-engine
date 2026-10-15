#pragma once

#include "RHI/RHITypes.h"

#include <memory>
#include <string>
#include <string_view>

namespace arc
{
    namespace rhi
    {
        class Device;
        class Shader;
    }

    /// Loads shaders compiled at build time from the Shaders/ folder next to the executable.
    ///
    /// A shader is identified by its source file name and stage: Triangle.hlsl's vertex stage is
    /// Shaders/Triangle.vs.dxil on D3D12 and Shaders/Triangle.vs.metallib on Metal.
    class ShaderLibrary
    {
    public:
        explicit ShaderLibrary(rhi::Device& device);

        /// Returns nullptr and logs the reason if the file is missing or invalid.
        [[nodiscard]] std::unique_ptr<rhi::Shader> load(std::string_view name, rhi::ShaderStage stage,
                                                        std::string_view entryPoint) const;

        /// UTF-8, with a trailing separator.
        [[nodiscard]] const std::string& directory() const { return m_directory; }

    private:
        rhi::Device& m_device;
        std::string m_directory;
    };
}

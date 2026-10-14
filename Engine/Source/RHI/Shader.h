#pragma once

#include "RHI/RHITypes.h"

#include <cstddef>
#include <span>
#include <string>

namespace arc::rhi
{
    struct ShaderDesc
    {
        ShaderStage stage = ShaderStage::Vertex;

        /// In the device's shaderFormat(). Copied or consumed during createShader().
        std::span<const std::byte> bytecode;

        /// The function to run. Kept from the HLSL source through SPIR-V and MSL, so the same
        /// name works on every backend.
        std::string entryPoint;
        std::string debugName;
    };

    /// One compiled shader stage, ready to be combined into a pipeline. Created by
    /// Device::createShader().
    class Shader
    {
    public:
        virtual ~Shader() = default;

        Shader(const Shader&) = delete;
        Shader& operator=(const Shader&) = delete;

        [[nodiscard]] ShaderStage stage() const { return m_stage; }
        [[nodiscard]] const std::string& entryPoint() const { return m_entryPoint; }
        [[nodiscard]] const std::string& debugName() const { return m_debugName; }

    protected:
        explicit Shader(const ShaderDesc& desc)
            : m_stage(desc.stage)
            , m_entryPoint(desc.entryPoint)
            , m_debugName(desc.debugName)
        {
        }

    private:
        ShaderStage m_stage;
        std::string m_entryPoint;
        std::string m_debugName;
    };
}

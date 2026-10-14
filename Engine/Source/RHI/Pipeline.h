#pragma once

#include "RHI/RHITypes.h"

#include <cstdint>
#include <string>
#include <vector>

namespace arc::rhi
{
    class Shader;

    /// One vertex input. D3D12 matches it to the shader by semantic name; Metal by position in
    /// the list (SPIRV-Cross numbers the inputs [[attribute(0)]], [[attribute(1)]], ... in the
    /// order the HLSL input struct declares them). So the attributes must be listed in the same
    /// order as the shader's input struct.
    struct VertexAttribute
    {
        std::string semantic{};      // HLSL semantic without the index, e.g. "POSITION".
        uint32_t semanticIndex = 0;
        VertexFormat format = VertexFormat::Float3;
        uint32_t offset = 0;         // Bytes from the start of the vertex.
    };

    /// The layout of the single interleaved vertex buffer a pipeline reads.
    struct VertexLayout
    {
        uint32_t stride = 0;         // Bytes per vertex.
        std::vector<VertexAttribute> attributes{};
    };

    /// Everything fixed when a pipeline is created. Changing any of it means another pipeline.
    struct PipelineDesc
    {
        const Shader* vertexShader = nullptr;
        const Shader* pixelShader = nullptr;
        VertexLayout vertexLayout{};

        /// Must match the format of the render target the pipeline draws into.
        Format colorFormat = Format::BGRA8_UNORM;

        CullMode cullMode = CullMode::Back;
        /// Counter-clockwise front faces: the convention of a right-handed world (see Math.h)
        /// and of glTF.
        bool frontCounterClockwise = true;

        std::string debugName{};
    };

    /// A compiled graphics pipeline: shaders, vertex layout, rasterizer and output state baked
    /// together so the driver does its compilation once, at creation, instead of at draw time.
    /// D3D12 pipeline state object, Metal render pipeline state.
    class Pipeline
    {
    public:
        virtual ~Pipeline() = default;

        Pipeline(const Pipeline&) = delete;
        Pipeline& operator=(const Pipeline&) = delete;

        [[nodiscard]] const PipelineDesc& desc() const { return m_desc; }

    protected:
        explicit Pipeline(const PipelineDesc& desc) : m_desc(desc) {}

    private:
        PipelineDesc m_desc;
    };
}

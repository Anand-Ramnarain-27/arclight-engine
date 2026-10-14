#pragma once

#include "Application/Module.h"

#include <memory>

namespace arc
{
    namespace rhi
    {
        class Buffer;
        class Pipeline;
    }

    /// Draws one coloured triangle into the backbuffer, behind the editor UI. The smallest
    /// complete use of the RHI: GPU-only vertex and index buffers, a pipeline and an indexed draw.
    /// Placeholder until the scene viewport exists.
    class TriangleModule final : public Module
    {
    public:
        explicit TriangleModule(Application& app);
        ~TriangleModule() override;

        [[nodiscard]] std::string_view name() const override { return "Triangle"; }

        [[nodiscard]] bool init() override;
        void render() override;
        void shutdown() override;

    private:
        std::unique_ptr<rhi::Pipeline> m_pipeline;
        std::unique_ptr<rhi::Buffer> m_vertexBuffer;
        std::unique_ptr<rhi::Buffer> m_indexBuffer;
    };
}

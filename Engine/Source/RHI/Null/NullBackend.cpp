#include "RHI/Backends.h"
#include "RHI/Buffer.h"
#include "RHI/CommandList.h"
#include "RHI/Device.h"
#include "RHI/Pipeline.h"
#include "RHI/Shader.h"
#include "RHI/ShaderUtils.h"
#include "RHI/Swapchain.h"
#include "RHI/Texture.h"

#include "Core/Assert.h"
#include "Core/Log.h"

#include <cstddef>
#include <cstring>
#include <vector>

namespace arc::rhi
{
    namespace
    {
        class NullTexture final : public Texture
        {
        public:
            void setDesc(const TextureDesc& desc) { m_desc = desc; }
        };

        // Backed by CPU memory, so copies really happen and upload/readback code can be
        // tested without a GPU.
        class NullBuffer final : public Buffer
        {
        public:
            explicit NullBuffer(const BufferDesc& desc) : m_storage(desc.size)
            {
                m_desc = desc;
                if (desc.memory != MemoryType::GpuOnly)
                    m_mapped = m_storage.data();
                m_state = desc.memory == MemoryType::Readback ? ResourceState::CopyDest
                        : desc.memory == MemoryType::Upload   ? ResourceState::CopySource
                                                              : ResourceState::Common;
            }

            [[nodiscard]] std::byte* bytes() { return m_storage.data(); }

        private:
            std::vector<std::byte> m_storage;
        };

        class NullShader final : public Shader
        {
        public:
            explicit NullShader(const ShaderDesc& desc) : Shader(desc) {}
        };

        class NullPipeline final : public Pipeline
        {
        public:
            explicit NullPipeline(const PipelineDesc& desc) : Pipeline(desc) {}
        };

        class NullSwapchain final : public Swapchain
        {
        public:
            explicit NullSwapchain(const SwapchainDesc& desc)
            {
                m_desc = desc;
                m_backbuffer.setDesc({desc.size, desc.format});
            }

            bool resize(Extent2D size) override
            {
                m_desc.size = size;
                m_backbuffer.setDesc({size, m_desc.format});
                return true;
            }

            Texture* acquireBackbuffer() override { return &m_backbuffer; }
            void setVSync(bool enabled) override { m_desc.vsync = enabled; }

        private:
            NullTexture m_backbuffer;
        };

        class NullCommandList final : public CommandList
        {
        public:
            void beginRenderPass(const RenderPassDesc& desc) override
            {
                ARC_ASSERT(!m_inRenderPass, "Render passes do not nest");
                ARC_ASSERT(desc.color.texture != nullptr);
                m_inRenderPass = true;
            }

            void endRenderPass() override
            {
                ARC_ASSERT(m_inRenderPass);
                m_inRenderPass = false;
                m_pipeline = nullptr;
                m_hasVertexBuffer = false;
                m_hasIndexBuffer = false;
            }

            void setPipeline(Pipeline& pipeline) override
            {
                ARC_ASSERT(m_inRenderPass, "setPipeline outside a render pass");
                m_pipeline = &pipeline;
            }

            void setVertexBuffer(Buffer& buffer, uint64_t offset) override
            {
                ARC_ASSERT(m_pipeline, "setVertexBuffer before setPipeline");
                ARC_ASSERT(buffer.state() == ResourceState::Common || buffer.state() == ResourceState::VertexBuffer,
                           "Vertex buffer '{}' is in state {}", buffer.desc().debugName, resourceStateName(buffer.state()));
                ARC_ASSERT(offset < buffer.desc().size);
                m_hasVertexBuffer = true;
            }

            void setIndexBuffer(Buffer& buffer, IndexFormat /*format*/, uint64_t offset) override
            {
                ARC_ASSERT(m_inRenderPass, "setIndexBuffer outside a render pass");
                ARC_ASSERT(buffer.state() == ResourceState::Common || buffer.state() == ResourceState::IndexBuffer,
                           "Index buffer '{}' is in state {}", buffer.desc().debugName, resourceStateName(buffer.state()));
                ARC_ASSERT(offset < buffer.desc().size);
                m_hasIndexBuffer = true;
            }

            void draw(uint32_t /*vertexCount*/, uint32_t /*instanceCount*/, uint32_t /*firstVertex*/,
                      uint32_t /*firstInstance*/) override
            {
                checkDrawState();
            }

            void drawIndexed(uint32_t /*indexCount*/, uint32_t /*instanceCount*/, uint32_t /*firstIndex*/,
                             int32_t /*baseVertex*/, uint32_t /*firstInstance*/) override
            {
                checkDrawState();
                ARC_ASSERT(m_hasIndexBuffer, "drawIndexed without an index buffer");
            }

            void copyBuffer(Buffer& dst, uint64_t dstOffset, Buffer& src, uint64_t srcOffset, uint64_t size) override
            {
                ARC_ASSERT(!m_inRenderPass, "Copies are not allowed inside a render pass");
                ARC_ASSERT(dst.state() == ResourceState::CopyDest, "Copy destination '{}' is in state {}",
                           dst.desc().debugName, resourceStateName(dst.state()));
                ARC_ASSERT(src.state() == ResourceState::CopySource, "Copy source '{}' is in state {}",
                           src.desc().debugName, resourceStateName(src.state()));
                ARC_ASSERT(dstOffset + size <= dst.desc().size && srcOffset + size <= src.desc().size);
                std::memcpy(static_cast<NullBuffer&>(dst).bytes() + dstOffset,
                            static_cast<NullBuffer&>(src).bytes() + srcOffset, size);
            }

            void barrier(Buffer& buffer, ResourceState state) override
            {
                ARC_ASSERT(!m_inRenderPass, "Barriers are not allowed inside a render pass");
                ARC_ASSERT(buffer.desc().memory == MemoryType::GpuOnly, "'{}' has a fixed state", buffer.desc().debugName);
                trackBufferState(buffer, state);
            }

            void pushDebugGroup(std::string_view /*name*/) override { ++m_debugDepth; }

            void popDebugGroup() override
            {
                ARC_ASSERT(m_debugDepth > 0, "Unbalanced popDebugGroup");
                --m_debugDepth;
            }

            void checkEndOfFrame() const
            {
                ARC_ASSERT(!m_inRenderPass, "Frame ended inside a render pass");
                ARC_ASSERT(m_debugDepth == 0, "Frame ended with open debug groups");
            }

        private:
            void checkDrawState() const
            {
                ARC_ASSERT(m_inRenderPass, "Draw outside a render pass");
                ARC_ASSERT(m_pipeline, "Draw without a pipeline");
                ARC_ASSERT(m_hasVertexBuffer || m_pipeline->desc().vertexLayout.attributes.empty(),
                           "Pipeline '{}' reads vertices but no vertex buffer is set", m_pipeline->desc().debugName);
            }

            Pipeline* m_pipeline = nullptr;
            bool m_hasVertexBuffer = false;
            bool m_hasIndexBuffer = false;
            bool m_inRenderPass = false;
            int m_debugDepth = 0;
        };

        class NullDevice final : public Device
        {
        public:
            NullDevice()
            {
                m_adapterInfo.backend = Backend::Null;
                m_adapterInfo.name = "Null device (no GPU work)";
            }

            const AdapterInfo& adapterInfo() const override { return m_adapterInfo; }

            std::unique_ptr<Swapchain> createSwapchain(Window& /*window*/, const SwapchainDesc& desc) override
            {
                return std::make_unique<NullSwapchain>(desc);
            }

            std::unique_ptr<Buffer> createBuffer(const BufferDesc& desc) override
            {
                ARC_ASSERT(desc.size > 0, "Buffer '{}' has zero size", desc.debugName);
                return std::make_unique<NullBuffer>(desc);
            }

            ShaderFormat shaderFormat() const override { return ShaderFormat::DXIL; }

            std::unique_ptr<Shader> createShader(const ShaderDesc& desc) override
            {
                if (!looksLikeDxil(desc.bytecode))
                {
                    ARC_LOG_ERROR("Shader '{}' is not DXIL", desc.debugName);
                    return nullptr;
                }
                return std::make_unique<NullShader>(desc);
            }

            std::unique_ptr<Pipeline> createPipeline(const PipelineDesc& desc) override
            {
                ARC_ASSERT(desc.vertexShader && desc.vertexShader->stage() == ShaderStage::Vertex);
                ARC_ASSERT(desc.pixelShader && desc.pixelShader->stage() == ShaderStage::Pixel);
                return std::make_unique<NullPipeline>(desc);
            }

            void submitImmediate(const std::function<void(CommandList&)>& record) override
            {
                NullCommandList commandList;
                record(commandList);
                commandList.checkEndOfFrame();
                commandList.decayBufferStates();
            }

            CommandList& beginFrame(Swapchain& /*swapchain*/) override { return m_commandList; }

            void endFrame(Swapchain& /*swapchain*/) override
            {
                m_commandList.checkEndOfFrame();
                m_commandList.decayBufferStates();
                advanceFrame();
            }

            void waitIdle() override {}

        private:
            AdapterInfo m_adapterInfo;
            NullCommandList m_commandList;
        };
    }

    std::unique_ptr<Device> createNullDevice(const DeviceDesc& /*desc*/)
    {
        ARC_LOG_WARN("Using the Null RHI backend: nothing will be drawn");
        return std::make_unique<NullDevice>();
    }
}

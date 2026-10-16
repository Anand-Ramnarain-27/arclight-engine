#include "RHI/Backends.h"
#include "RHI/Buffer.h"
#include "RHI/CommandList.h"
#include "RHI/Device.h"
#include "RHI/Pipeline.h"
#include "RHI/Sampler.h"
#include "RHI/Shader.h"
#include "RHI/ShaderUtils.h"
#include "RHI/Swapchain.h"
#include "RHI/Texture.h"
#include "RHI/UploadRing.h"

#include "Core/Assert.h"
#include "Core/Log.h"

#include <cstddef>
#include <cstring>
#include <vector>

namespace arc::rhi
{
    namespace
    {
        // Backed by CPU memory like NullBuffer, one tightly packed array per mip level, so texture
        // copies really happen and the copy layout can be tested without a GPU.
        class NullTexture final : public Texture
        {
        public:
            NullTexture() = default;

            explicit NullTexture(const TextureDesc& desc)
            {
                m_desc = desc;
                m_levels.resize(desc.mipLevels);
                for (uint32_t mip = 0; mip < desc.mipLevels; ++mip)
                {
                    const Extent2D size = mipSize(desc.size, mip);
                    m_levels[mip].resize(uint64_t{size.width} * size.height * bytesPerPixel(desc.format));
                }
            }

            void setDesc(const TextureDesc& desc) { m_desc = desc; }

            [[nodiscard]] std::byte* level(uint32_t mip) { return m_levels[mip].data(); }

        private:
            std::vector<std::vector<std::byte>> m_levels;
        };

        class NullSampler final : public Sampler
        {
        public:
            explicit NullSampler(const SamplerDesc& desc) : Sampler(desc) {}
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
                m_backbuffer.setDesc(backbufferDesc());
            }

            bool resize(Extent2D size) override
            {
                m_desc.size = size;
                m_backbuffer.setDesc(backbufferDesc());
                return true;
            }

            Texture* acquireBackbuffer() override { return &m_backbuffer; }
            void setVSync(bool enabled) override { m_desc.vsync = enabled; }

        private:
            [[nodiscard]] TextureDesc backbufferDesc() const
            {
                return {.size = m_desc.size, .format = m_desc.format, .usage = TextureUsage::RenderTarget,
                        .debugName = "Backbuffer"};
            }

            NullTexture m_backbuffer;
        };

        class NullCommandList final : public CommandList
        {
        public:
            void beginRenderPass(const RenderPassDesc& desc) override
            {
                ARC_ASSERT(!m_inRenderPass, "Render passes do not nest");
                validateRenderPass(desc);
                desc.color.texture->setState(ResourceState::RenderTarget);
                if (desc.depth.texture)
                    desc.depth.texture->setState(ResourceState::DepthWrite);
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
                validatePipelineForPass(pipeline.desc());
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

            void setTexture(uint32_t slot, Texture& texture) override
            {
                ARC_ASSERT(m_inRenderPass, "setTexture outside a render pass");
                validateTextureBinding(slot, texture);
            }

            void setSampler(uint32_t slot, Sampler& /*sampler*/) override
            {
                ARC_ASSERT(m_inRenderPass, "setSampler outside a render pass");
                validateSamplerBinding(slot);
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

            void copyBufferToTexture(Texture& dst, uint32_t mipLevel, Buffer& src, uint64_t srcOffset,
                                     uint32_t srcRowPitch) override
            {
                ARC_ASSERT(!m_inRenderPass, "Copies are not allowed inside a render pass");
                validateTextureCopy(dst, mipLevel, ResourceState::CopyDest, src, srcOffset, srcRowPitch,
                                    ResourceState::CopySource);
                copyRows(static_cast<NullTexture&>(dst).level(mipLevel), rowBytes(dst, mipLevel),
                         static_cast<NullBuffer&>(src).bytes() + srcOffset, srcRowPitch, mipSize(dst.desc().size, mipLevel).height);
            }

            void copyTextureToBuffer(Buffer& dst, uint64_t dstOffset, uint32_t dstRowPitch, Texture& src,
                                     uint32_t mipLevel) override
            {
                ARC_ASSERT(!m_inRenderPass, "Copies are not allowed inside a render pass");
                validateTextureCopy(src, mipLevel, ResourceState::CopySource, dst, dstOffset, dstRowPitch,
                                    ResourceState::CopyDest);
                const uint32_t bytes = rowBytes(src, mipLevel);
                const uint32_t height = mipSize(src.desc().size, mipLevel).height;
                std::byte* out = static_cast<NullBuffer&>(dst).bytes() + dstOffset;
                const std::byte* in = static_cast<NullTexture&>(src).level(mipLevel);
                for (uint32_t row = 0; row < height; ++row)
                    std::memcpy(out + uint64_t{row} * dstRowPitch, in + uint64_t{row} * bytes, bytes);
            }

            void barrier(Buffer& buffer, ResourceState state) override
            {
                ARC_ASSERT(!m_inRenderPass, "Barriers are not allowed inside a render pass");
                ARC_ASSERT(buffer.desc().memory == MemoryType::GpuOnly, "'{}' has a fixed state", buffer.desc().debugName);
                trackBufferState(buffer, state);
            }

            void barrier(Texture& texture, ResourceState state) override
            {
                ARC_ASSERT(!m_inRenderPass, "Barriers are not allowed inside a render pass");
                texture.setState(state);
            }

            void pushDebugGroup(std::string_view /*name*/) override { ++m_debugDepth; }

            void popDebugGroup() override
            {
                ARC_ASSERT(m_debugDepth > 0, "Unbalanced popDebugGroup");
                --m_debugDepth;
            }

            void begin(UploadRing& ring, uint32_t region) { setUploadRing(ring, region); }

            void checkEndOfFrame() const
            {
                ARC_ASSERT(!m_inRenderPass, "Frame ended inside a render pass");
                ARC_ASSERT(m_debugDepth == 0, "Frame ended with open debug groups");
            }

        protected:
            void bindConstants(uint32_t /*slot*/, Buffer& /*buffer*/, uint64_t /*offset*/) override
            {
                ARC_ASSERT(m_inRenderPass, "setConstants outside a render pass");
            }

        private:
            static uint32_t rowBytes(const Texture& texture, uint32_t mipLevel)
            {
                return mipSize(texture.desc().size, mipLevel).width * bytesPerPixel(texture.desc().format);
            }

            // Tightly packed destination rows from pitched source rows.
            static void copyRows(std::byte* dst, uint32_t dstRowBytes, const std::byte* src, uint32_t srcRowPitch,
                                 uint32_t rows)
            {
                for (uint32_t row = 0; row < rows; ++row)
                    std::memcpy(dst + uint64_t{row} * dstRowBytes, src + uint64_t{row} * srcRowPitch, dstRowBytes);
            }

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
                // NullDevice is final, so the virtual createBufferImpl() reached from here is its own.
                const bool ringCreated = m_uploadRing.init(*this, 256 * 1024, "Upload Ring");
                ARC_VERIFY(ringCreated);
            }

            const AdapterInfo& adapterInfo() const override { return m_adapterInfo; }

            std::unique_ptr<Swapchain> createSwapchain(Window& /*window*/, const SwapchainDesc& desc) override
            {
                return std::make_unique<NullSwapchain>(desc);
            }

            std::unique_ptr<Buffer> createBufferImpl(const BufferDesc& desc) override
            {
                return std::make_unique<NullBuffer>(desc);
            }

            std::unique_ptr<Texture> createTextureImpl(const TextureDesc& desc) override
            {
                return std::make_unique<NullTexture>(desc);
            }

            std::unique_ptr<Sampler> createSamplerImpl(const SamplerDesc& desc) override
            {
                return std::make_unique<NullSampler>(desc);
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
                m_uploadRing.reset(UploadRing::kImmediateRegion);
                NullCommandList commandList;
                commandList.begin(m_uploadRing, UploadRing::kImmediateRegion);
                record(commandList);
                commandList.checkEndOfFrame();
                commandList.decayBufferStates();
            }

            CommandList& beginFrame(Swapchain& /*swapchain*/) override
            {
                m_uploadRing.reset(frameSlot());
                m_commandList.begin(m_uploadRing, frameSlot());
                return m_commandList;
            }

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
            UploadRing m_uploadRing;
        };
    }

    std::unique_ptr<Device> createNullDevice(const DeviceDesc& /*desc*/)
    {
        ARC_LOG_WARN("Using the Null RHI backend: nothing will be drawn");
        return std::make_unique<NullDevice>();
    }
}

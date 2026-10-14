#include "RHI/Backends.h"
#include "RHI/Buffer.h"
#include "RHI/CommandList.h"
#include "RHI/Device.h"
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

#include "RHI/Backends.h"
#include "RHI/CommandList.h"
#include "RHI/Device.h"
#include "RHI/Swapchain.h"
#include "RHI/Texture.h"

#include "Core/Assert.h"
#include "Core/Log.h"

namespace arc::rhi
{
    namespace
    {
        class NullTexture final : public Texture
        {
        public:
            void setDesc(const TextureDesc& desc) { m_desc = desc; }
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

            CommandList& beginFrame(Swapchain& /*swapchain*/) override { return m_commandList; }

            void endFrame(Swapchain& /*swapchain*/) override
            {
                m_commandList.checkEndOfFrame();
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

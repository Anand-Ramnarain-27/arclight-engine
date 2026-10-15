#include "UI/ImGuiRenderBackend.h"

#include "Core/Assert.h"
#include "RHI/D3D12/D3D12Backend.h"
#include "Renderer/Renderer.h"

#include <imgui.h>
#include <imgui_impl_dx12.h>

#include <unordered_map>

namespace arc
{
    namespace
    {
        class D3D12ImGuiRenderBackend final : public ImGuiRenderBackend
        {
        public:
            explicit D3D12ImGuiRenderBackend(Renderer& renderer)
                : m_device(static_cast<rhi::d3d12::D3D12Device&>(renderer.device()))
                , m_rtvFormat(rhi::d3d12::toDxgiFormat(renderer.swapchain().desc().format))
            {
            }

            bool init() override
            {
                // ImGui's textures (the font atlas, and any image shown in the UI) take persistent
                // descriptors from the device's shader-visible heap, the one every command list has
                // bound, so drawing the UI never switches heaps.
                ImGui_ImplDX12_InitInfo info;
                info.Device = m_device.native();
                info.CommandQueue = m_device.queue();
                info.NumFramesInFlight = static_cast<int>(rhi::kMaxFramesInFlight);
                info.RTVFormat = m_rtvFormat;
                info.DSVFormat = DXGI_FORMAT_UNKNOWN;
                info.UserData = &m_device;
                info.SrvDescriptorHeap = m_device.shaderVisibleSrvHeap().native();
                info.SrvDescriptorAllocFn = [](ImGui_ImplDX12_InitInfo* initInfo, D3D12_CPU_DESCRIPTOR_HANDLE* cpu,
                                               D3D12_GPU_DESCRIPTOR_HANDLE* gpu) {
                    auto* device = static_cast<rhi::d3d12::D3D12Device*>(initInfo->UserData);
                    const rhi::d3d12::D3D12Descriptor descriptor = device->shaderVisibleSrvHeap().allocate();
                    ARC_ASSERT(descriptor.valid(), "No descriptor left for an ImGui texture");
                    *cpu = descriptor.cpu;
                    *gpu = descriptor.gpu;
                };
                info.SrvDescriptorFreeFn = [](ImGui_ImplDX12_InitInfo* initInfo, D3D12_CPU_DESCRIPTOR_HANDLE cpu,
                                              D3D12_GPU_DESCRIPTOR_HANDLE /*gpu*/) {
                    static_cast<rhi::d3d12::D3D12Device*>(initInfo->UserData)->shaderVisibleSrvHeap().free(cpu);
                };

                return ImGui_ImplDX12_Init(&info);
            }

            void shutdown() override
            {
                // The device has waited for the GPU before modules shut down.
                for (auto& [key, entry] : m_textureIds)
                    m_device.shaderVisibleSrvHeap().free(entry.descriptor);
                m_textureIds.clear();
                ImGui_ImplDX12_Shutdown();
            }

            void newFrame() override
            {
                releaseStaleTextureIds();
                ImGui_ImplDX12_NewFrame();
            }

            void render(rhi::CommandList& commandList, ImDrawData* drawData) override
            {
                auto& d3dCommandList = static_cast<rhi::d3d12::D3D12CommandList&>(commandList);
                ImGui_ImplDX12_RenderDrawData(drawData, d3dCommandList.native());
                // ImGui set its own pipeline, root signature and tables.
                d3dCommandList.invalidateBindings();
            }

            uint64_t textureId(rhi::Texture& texture) override
            {
                // ImGui's shader reads its texture through a descriptor table in the shader-visible
                // heap, so each texture shown needs its SRV copied there once. The copy is cached by
                // the texture's handle; a recreated texture has a new handle and gets a new copy.
                const rhi::TextureHandle handle = texture.handle();
                ARC_ASSERT(!handle.isNull(), "Only textures created by the device can be shown in ImGui");
                const uint64_t key = (uint64_t{handle.index} << 32) | handle.generation;

                auto [it, inserted] = m_textureIds.try_emplace(key);
                CachedTextureId& entry = it->second;
                if (inserted)
                {
                    entry.handle = handle;
                    entry.descriptor = m_device.shaderVisibleSrvHeap().allocate();
                    ARC_ASSERT(entry.descriptor.valid(), "No shader-visible descriptor left for an ImGui texture");
                    m_device.native()->CopyDescriptorsSimple(1, entry.descriptor.cpu,
                                                             static_cast<rhi::d3d12::D3D12Texture&>(texture).srv,
                                                             D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
                }
                entry.lastUsedFrame = m_device.frameNumber();
                return entry.descriptor.gpu.ptr;
            }

        private:
            struct CachedTextureId
            {
                rhi::TextureHandle handle;
                rhi::d3d12::D3D12Descriptor descriptor;
                uint64_t lastUsedFrame = 0;
            };

            // A copy is freed once its texture is gone and the last frame that drew it has finished
            // on the GPU. During frame N's update, frame N - kMaxFramesInFlight - 1 is the newest
            // one guaranteed complete.
            void releaseStaleTextureIds()
            {
                std::erase_if(m_textureIds, [this](auto& item) {
                    CachedTextureId& entry = item.second;
                    const bool textureGone = m_device.find(entry.handle) == nullptr;
                    const bool gpuDone = m_device.frameNumber() > entry.lastUsedFrame + rhi::kMaxFramesInFlight;
                    if (!textureGone || !gpuDone)
                        return false;
                    m_device.shaderVisibleSrvHeap().free(entry.descriptor);
                    return true;
                });
            }

            rhi::d3d12::D3D12Device& m_device;
            DXGI_FORMAT m_rtvFormat;
            std::unordered_map<uint64_t, CachedTextureId> m_textureIds;
        };
    }

    std::unique_ptr<ImGuiRenderBackend> createImGuiRenderBackend(Renderer& renderer)
    {
        return std::make_unique<D3D12ImGuiRenderBackend>(renderer);
    }
}

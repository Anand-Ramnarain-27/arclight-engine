#include "UI/ImGuiRenderBackend.h"

#include "Core/Assert.h"
#include "RHI/D3D12/D3D12Backend.h"
#include "Renderer/Renderer.h"

#include <imgui.h>
#include <imgui_impl_dx12.h>

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

            void shutdown() override { ImGui_ImplDX12_Shutdown(); }

            void newFrame() override { ImGui_ImplDX12_NewFrame(); }

            void render(rhi::CommandList& commandList, ImDrawData* drawData) override
            {
                auto& d3dCommandList = static_cast<rhi::d3d12::D3D12CommandList&>(commandList);
                ImGui_ImplDX12_RenderDrawData(drawData, d3dCommandList.native());
                // ImGui set its own pipeline, root signature and tables.
                d3dCommandList.invalidateBindings();
            }

        private:
            rhi::d3d12::D3D12Device& m_device;
            DXGI_FORMAT m_rtvFormat;
        };
    }

    std::unique_ptr<ImGuiRenderBackend> createImGuiRenderBackend(Renderer& renderer)
    {
        return std::make_unique<D3D12ImGuiRenderBackend>(renderer);
    }
}

#include "UI/ImGuiRenderBackend.h"

#include "Core/Assert.h"
#include "Core/Log.h"
#include "RHI/D3D12/D3D12Backend.h"
#include "Renderer/Renderer.h"

#include <imgui.h>
#include <imgui_impl_dx12.h>

#include <vector>

namespace arc
{
    namespace
    {
        using rhi::d3d12::ComPtr;

        // ImGui needs one shader-visible SRV per texture it draws (the font atlas, plus any
        // images shown in the UI). The heap is owned here until the engine has a shared
        // descriptor allocator.
        // TODO: Allocate from the engine's shader-visible heap once it exists (Phase 2), so the
        // scene viewport texture can be shown without a second heap.
        constexpr UINT kSrvCapacity = 64;

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
                ID3D12Device* device = m_device.native();

                D3D12_DESCRIPTOR_HEAP_DESC heapDesc{};
                heapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
                heapDesc.NumDescriptors = kSrvCapacity;
                heapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
                if (!rhi::d3d12::succeeded(device->CreateDescriptorHeap(&heapDesc, IID_PPV_ARGS(&m_srvHeap)),
                                           "CreateDescriptorHeap(ImGui SRV)"))
                    return false;
                rhi::d3d12::setDebugName(m_srvHeap.Get(), "ImGui SRV Heap");

                m_srvStride = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
                m_freeSlots.reserve(kSrvCapacity);
                for (UINT i = kSrvCapacity; i > 0; --i)
                    m_freeSlots.push_back(i - 1);

                ImGui_ImplDX12_InitInfo info;
                info.Device = device;
                info.CommandQueue = m_device.queue();
                info.NumFramesInFlight = static_cast<int>(rhi::kMaxFramesInFlight);
                info.RTVFormat = m_rtvFormat;
                info.DSVFormat = DXGI_FORMAT_UNKNOWN;
                info.UserData = this;
                info.SrvDescriptorHeap = m_srvHeap.Get();
                info.SrvDescriptorAllocFn = [](ImGui_ImplDX12_InitInfo* initInfo, D3D12_CPU_DESCRIPTOR_HANDLE* cpu,
                                               D3D12_GPU_DESCRIPTOR_HANDLE* gpu) {
                    static_cast<D3D12ImGuiRenderBackend*>(initInfo->UserData)->allocate(cpu, gpu);
                };
                info.SrvDescriptorFreeFn = [](ImGui_ImplDX12_InitInfo* initInfo, D3D12_CPU_DESCRIPTOR_HANDLE cpu,
                                              D3D12_GPU_DESCRIPTOR_HANDLE gpu) {
                    static_cast<D3D12ImGuiRenderBackend*>(initInfo->UserData)->free(cpu, gpu);
                };

                return ImGui_ImplDX12_Init(&info);
            }

            void shutdown() override
            {
                ImGui_ImplDX12_Shutdown();
                m_srvHeap.Reset();
            }

            void newFrame() override { ImGui_ImplDX12_NewFrame(); }

            void render(rhi::CommandList& commandList, ImDrawData* drawData) override
            {
                auto& d3dCommandList = static_cast<rhi::d3d12::D3D12CommandList&>(commandList);
                ImGui_ImplDX12_RenderDrawData(drawData, d3dCommandList.native());
            }

        private:
            void allocate(D3D12_CPU_DESCRIPTOR_HANDLE* cpu, D3D12_GPU_DESCRIPTOR_HANDLE* gpu)
            {
                ARC_ASSERT(!m_freeSlots.empty(), "ImGui SRV heap is full ({} descriptors)", kSrvCapacity);
                const UINT slot = m_freeSlots.back();
                m_freeSlots.pop_back();

                cpu->ptr = m_srvHeap->GetCPUDescriptorHandleForHeapStart().ptr + static_cast<SIZE_T>(slot) * m_srvStride;
                gpu->ptr = m_srvHeap->GetGPUDescriptorHandleForHeapStart().ptr + static_cast<UINT64>(slot) * m_srvStride;
            }

            void free(D3D12_CPU_DESCRIPTOR_HANDLE cpu, D3D12_GPU_DESCRIPTOR_HANDLE /*gpu*/)
            {
                const SIZE_T offset = cpu.ptr - m_srvHeap->GetCPUDescriptorHandleForHeapStart().ptr;
                m_freeSlots.push_back(static_cast<UINT>(offset / m_srvStride));
            }

            rhi::d3d12::D3D12Device& m_device;
            DXGI_FORMAT m_rtvFormat;
            ComPtr<ID3D12DescriptorHeap> m_srvHeap;
            UINT m_srvStride = 0;
            std::vector<UINT> m_freeSlots;
        };
    }

    std::unique_ptr<ImGuiRenderBackend> createImGuiRenderBackend(Renderer& renderer)
    {
        return std::make_unique<D3D12ImGuiRenderBackend>(renderer);
    }
}

#include "RHI/D3D12/D3D12Backend.h"

#include "Core/Assert.h"

#include <string>

namespace arc::rhi::d3d12
{
    namespace
    {
        // PIX and RenderDoc decode BeginEvent payloads by this metadata value; 1 means a
        // null-terminated ANSI string (WINPIX_EVENT_ANSI_VERSION in pix3.h).
        constexpr UINT kEventMetadataAnsi = 1;

        void beginEvent(ID3D12GraphicsCommandList* list, std::string_view name)
        {
            const std::string text(name);
            list->BeginEvent(kEventMetadataAnsi, text.c_str(), static_cast<UINT>(text.size() + 1));
        }
    }

    bool D3D12CommandList::init(ID3D12Device* device, ID3D12CommandAllocator* allocator)
    {
        if (!succeeded(device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, allocator, nullptr,
                                                 IID_PPV_ARGS(&m_list)),
                       "CreateCommandList"))
            return false;
        setDebugName(m_list.Get(), "Frame Command List");

        // Command lists are created open; begin() expects a closed one.
        return close();
    }

    void D3D12CommandList::begin(ID3D12CommandAllocator* allocator)
    {
        ARC_VERIFY(SUCCEEDED(m_list->Reset(allocator, nullptr)));
    }

    bool D3D12CommandList::close()
    {
        ARC_ASSERT(!m_inRenderPass, "Command list closed inside a render pass");
        ARC_ASSERT(m_debugDepth == 0, "Command list closed with {} open debug group(s)", m_debugDepth);
        return succeeded(m_list->Close(), "ID3D12GraphicsCommandList::Close");
    }

    void D3D12CommandList::transition(D3D12Texture& texture, D3D12_RESOURCE_STATES state)
    {
        if (texture.state == state)
            return;

        D3D12_RESOURCE_BARRIER barrier{};
        barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        barrier.Transition.pResource = texture.resource();
        barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        barrier.Transition.StateBefore = texture.state;
        barrier.Transition.StateAfter = state;
        m_list->ResourceBarrier(1, &barrier);

        texture.state = state;
    }

    void D3D12CommandList::beginRenderPass(const RenderPassDesc& desc)
    {
        ARC_ASSERT(!m_inRenderPass, "Render passes do not nest");
        ARC_ASSERT(desc.color.texture != nullptr);
        m_inRenderPass = true;

        m_passHasDebugGroup = !desc.debugName.empty();
        if (m_passHasDebugGroup)
            beginEvent(m_list.Get(), desc.debugName);

        auto& target = static_cast<D3D12Texture&>(*desc.color.texture);
        ARC_ASSERT(target.rtv.ptr != 0, "Texture is not a render target");
        transition(target, D3D12_RESOURCE_STATE_RENDER_TARGET);

        m_list->OMSetRenderTargets(1, &target.rtv, FALSE, nullptr);

        if (desc.color.load == LoadOp::Clear)
        {
            const ClearColor& c = desc.color.clearColor;
            const float color[] = {c.r, c.g, c.b, c.a};
            m_list->ClearRenderTargetView(target.rtv, color, 0, nullptr);
        }

        // Metal resets the viewport and scissor to the full target at the start of every pass;
        // doing the same here keeps the two backends consistent.
        const Extent2D size = target.desc().size;
        const D3D12_VIEWPORT viewport{0.0f, 0.0f, static_cast<float>(size.width), static_cast<float>(size.height),
                                      0.0f, 1.0f};
        const D3D12_RECT scissor{0, 0, static_cast<LONG>(size.width), static_cast<LONG>(size.height)};
        m_list->RSSetViewports(1, &viewport);
        m_list->RSSetScissorRects(1, &scissor);
    }

    void D3D12CommandList::endRenderPass()
    {
        ARC_ASSERT(m_inRenderPass);
        m_inRenderPass = false;

        if (m_passHasDebugGroup)
            m_list->EndEvent();
        m_passHasDebugGroup = false;
    }

    void D3D12CommandList::pushDebugGroup(std::string_view name)
    {
        beginEvent(m_list.Get(), name);
        ++m_debugDepth;
    }

    void D3D12CommandList::popDebugGroup()
    {
        ARC_ASSERT(m_debugDepth > 0, "Unbalanced popDebugGroup");
        m_list->EndEvent();
        --m_debugDepth;
    }
}

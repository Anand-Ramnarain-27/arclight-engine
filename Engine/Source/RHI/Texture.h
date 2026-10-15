#pragma once

#include "RHI/RHITypes.h"

namespace arc::rhi
{
    /// A 2D image in GPU memory, optionally with mipmaps. Created by Device::createTexture(), or
    /// owned by a swapchain for its backbuffers.
    class Texture
    {
    public:
        virtual ~Texture() = default;

        Texture(const Texture&) = delete;
        Texture& operator=(const Texture&) = delete;

        [[nodiscard]] const TextureDesc& desc() const { return m_desc; }

        /// The state at the current point of command recording, set by CommandList::barrier() and
        /// by render passes. Unlike buffers, textures keep their state across submissions: D3D12
        /// returns every buffer to COMMON at the end of a submission, but a texture stays in the
        /// state its last barrier set.
        [[nodiscard]] ResourceState state() const { return m_state; }
        void setState(ResourceState state) { m_state = state; }

    protected:
        Texture() = default;

        TextureDesc m_desc;
        ResourceState m_state = ResourceState::Common;
    };
}

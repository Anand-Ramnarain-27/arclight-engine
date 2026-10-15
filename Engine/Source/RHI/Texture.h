#pragma once

#include "RHI/RHITypes.h"

namespace arc::rhi
{
    class Texture
    {
    public:
        virtual ~Texture() = default;

        Texture(const Texture&) = delete;
        Texture& operator=(const Texture&) = delete;

        [[nodiscard]] const TextureDesc& desc() const { return m_desc; }

    protected:
        Texture() = default;

        TextureDesc m_desc;
    };
}

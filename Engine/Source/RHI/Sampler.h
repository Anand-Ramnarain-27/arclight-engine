#pragma once

#include "RHI/RHITypes.h"

namespace arc::rhi
{
    /// How a shader reads a texture: filtering, addressing and the usable mip range. Created by
    /// Device::createSampler(). Holds no GPU memory, so creating one is cheap, but it is still
    /// done at load time or when a setting changes, not per draw.
    class Sampler
    {
    public:
        virtual ~Sampler() = default;

        Sampler(const Sampler&) = delete;
        Sampler& operator=(const Sampler&) = delete;

        [[nodiscard]] const SamplerDesc& desc() const { return m_desc; }

    protected:
        explicit Sampler(const SamplerDesc& desc) : m_desc(desc) {}

    private:
        SamplerDesc m_desc;
    };
}

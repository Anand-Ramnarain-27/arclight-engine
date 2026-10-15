#pragma once

#include "RHI/RHITypes.h"

namespace arc::rhi
{
    class Device;

    /// How a shader reads a texture: filtering, addressing and the usable mip range. Created by
    /// Device::createSampler(). Holds no GPU memory, so creating one is cheap, but it is still
    /// done at load time or when a setting changes, not per draw.
    class Sampler
    {
    public:
        virtual ~Sampler();

        Sampler(const Sampler&) = delete;
        Sampler& operator=(const Sampler&) = delete;

        [[nodiscard]] const SamplerDesc& desc() const { return m_desc; }

        [[nodiscard]] SamplerHandle handle() const { return m_handle; }

    protected:
        explicit Sampler(const SamplerDesc& desc) : m_desc(desc) {}

    private:
        friend class Device;
        SamplerDesc m_desc;
        Device* m_owner = nullptr;
        SamplerHandle m_handle;
    };
}

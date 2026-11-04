#pragma once

#include <array>
#include <cstdint>
#include <memory>
#include <string_view>

namespace arc
{
    class ShaderLibrary;

    namespace rhi
    {
        class Device;
        class Pipeline;
        class Sampler;
        class Texture;
    }

    /// Image-based lighting: turns an environment (the sky) into the textures Lighting.hlsli reads to
    /// light every surface with the light arriving from all directions, on the GPU at load time
    /// (Engine/Shaders/Ibl*.hlsl).
    ///
    ///   radiance     The environment as an RGBA16F cube with a full mip chain: what the sky shows.
    ///   irradiance   32 px a face: the cosine-weighted light over the hemisphere around each normal,
    ///                divided by pi, for diffuse.
    ///   prefiltered  128 px, kPrefilteredLevels mips: the radiance convolved with GGX lobes of
    ///                roughness 0, 0.25 ... 1, one per mip, for specular reflections.
    ///   BRDF table   128 x 128 RGBA16F: the split-sum's (A, B) by (cos(n, v), roughness). Depends on
    ///                the BRDF only, so it is made once by init().
    ///
    /// A bake replaces the three cubes; frames in flight still reading the old ones keep them alive
    /// (see Device). Each bake is a submitImmediate(), so it blocks for as long as the GPU takes,
    /// tens of milliseconds for a 1024 px sky.
    class EnvironmentMaps
    {
    public:
        static constexpr uint32_t kIrradianceSize = 32;
        static constexpr uint32_t kPrefilteredSize = 128;
        static constexpr uint32_t kPrefilteredLevels = 5;
        static constexpr uint32_t kBrdfLutSize = 128;
        /// The largest radiance cube a bake makes: a bigger source is read at a smaller mip.
        static constexpr uint32_t kMaxRadianceSize = 1024;

        EnvironmentMaps();
        ~EnvironmentMaps();

        EnvironmentMaps(const EnvironmentMaps&) = delete;
        EnvironmentMaps& operator=(const EnvironmentMaps&) = delete;

        /// Creates the pipelines and samplers and bakes the BRDF table. Returns false and logs the
        /// reason on failure.
        [[nodiscard]] bool init(rhi::Device& device, const ShaderLibrary& shaders);
        void shutdown();

        /// From an equirectangular (latitude-longitude) RGBA16F texture, twice as wide as it is tall,
        /// with a full mip chain, in the ShaderRead state. Its centre column faces -Z. The radiance
        /// cube's faces are a quarter of its width (a power of two, 16 to kMaxRadianceSize). Returns
        /// false and logs on failure, keeping the previous maps.
        [[nodiscard]] bool bakeFromEquirect(rhi::Texture& equirect, std::string_view name);

        /// From a cube texture of any colour format, with a full mip chain, in the ShaderRead state.
        /// Returns false and logs on failure, keeping the previous maps.
        [[nodiscard]] bool bakeFromCube(rhi::Texture& cube, std::string_view name);

        /// Null before the first bake.
        [[nodiscard]] rhi::Texture* radiance() const { return m_radiance.get(); }
        [[nodiscard]] rhi::Texture* irradiance() const { return m_irradiance.get(); }
        [[nodiscard]] rhi::Texture* prefiltered() const { return m_prefiltered.get(); }
        [[nodiscard]] rhi::Texture* brdfLut() const { return m_brdfLut.get(); }
        /// Trilinear and clamped: for every texture above.
        [[nodiscard]] rhi::Sampler* sampler() const { return m_cubeSampler.get(); }

        /// The prefiltered cube's last mip, which holds roughness 1.
        [[nodiscard]] static constexpr float prefilteredMaxLod() { return static_cast<float>(kPrefilteredLevels - 1); }

        /// The face size bakeFromEquirect() uses for an image `width` pixels wide.
        [[nodiscard]] static uint32_t radianceSizeForEquirect(uint32_t width);

    private:
        enum Pass : uint32_t
        {
            kFromEquirect,
            kFromCube,
            kIrradiance,
            kPrefilter,
            kBrdfLut,
            kPassCount,
        };

        // Makes the radiance cube from `source` with `pass`, then the cubes made from it.
        bool bake(rhi::Texture& source, Pass pass, uint32_t radianceSize, float sourceTexelsPerFace, std::string_view name);

        rhi::Device* m_device = nullptr;
        std::array<std::unique_ptr<rhi::Pipeline>, kPassCount> m_pipelines;
        std::unique_ptr<rhi::Sampler> m_equirectSampler;
        std::unique_ptr<rhi::Sampler> m_cubeSampler;
        std::unique_ptr<rhi::Texture> m_radiance;
        std::unique_ptr<rhi::Texture> m_irradiance;
        std::unique_ptr<rhi::Texture> m_prefiltered;
        std::unique_ptr<rhi::Texture> m_brdfLut;
    };
}

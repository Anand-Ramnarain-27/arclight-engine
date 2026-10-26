#pragma once

#include "Math/Math.h"
#include "Renderer/EnvironmentMaps.h"
#include "Renderer/Image.h"
#include "Renderer/Lighting.h"

#include <array>
#include <memory>
#include <string>

namespace arc
{
    class Renderer;

    namespace rhi
    {
        class Pipeline;
        class Sampler;
    }

    /// Draws the environment behind everything in the scene (Engine/Shaders/Skybox.hlsl), and owns
    /// the image-based lighting made from it (EnvironmentMaps), so the sky the scene shows is the sky
    /// that lights it.
    ///
    /// Starts with a procedural sky, a gradient from horizon to zenith with a sun where the default
    /// light (defaultSunLight()) comes from; load() replaces it with an HDR image or six face images.
    class Skybox
    {
    public:
        Skybox();
        ~Skybox();

        Skybox(const Skybox&) = delete;
        Skybox& operator=(const Skybox&) = delete;

        /// Returns false and logs the reason on failure.
        [[nodiscard]] bool init(Renderer& renderer);
        void shutdown();

        /// An equirectangular .hdr file (see loadEquirect()), or a folder of face images (see
        /// loadFaces()). Keeps the current sky and logs on failure.
        bool load(Renderer& renderer, const std::string& path);
        /// Loads px, nx, py, ny, pz and nz (.png, .jpg, .tga or .bmp) from `folder`: square images,
        /// all the same size, laid out for a left-handed space with +Z forward, as most published
        /// skyboxes are. Keeps the current sky and logs on failure.
        bool loadFaces(Renderer& renderer, const std::string& folder);
        /// Loads a Radiance .hdr latitude-longitude image, twice as wide as it is tall, its centre
        /// facing -Z: the usual layout of HDR skies, which keep the sun's real brightness. Images
        /// wider than kMaxEquirectWidth are halved until they fit. Keeps the current sky and logs on
        /// failure.
        bool loadEquirect(Renderer& renderer, const std::string& path);
        /// Back to the procedural sky.
        bool useProceduralSky(Renderer& renderer);

        /// Must be called inside a scene pass, after the opaque meshes.
        void draw(Renderer& renderer);

        [[nodiscard]] const std::string& source() const { return m_source; }

        /// The lighting made from the sky, for MeshRenderer::setLighting(). Its textures change when
        /// another sky is loaded.
        [[nodiscard]] EnvironmentLighting environmentLighting() const;

        static constexpr uint32_t kMaxEquirectWidth = 4096;

        bool visible = true;
        /// Multiplies both the sky as drawn and the light it gives.
        float intensity = 1.0f;
        /// Radians about +Y, counter-clockwise seen from above: turns the sky and its lighting
        /// together, to put the sun where the scene needs it.
        float rotation = 0.0f;

        /// The procedural sky's six faces, in the order of CubeFace, `size` pixels square, sRGB.
        [[nodiscard]] static std::array<Image, 6> proceduralSky(uint32_t size);

    private:
        bool setFaces(Renderer& renderer, const std::array<Image, 6>& faces, std::string source);

        std::array<std::unique_ptr<rhi::Pipeline>, 2> m_pipelines;   // Indexed by Camera::reversedZ.
        // The sky is drawn from the environment's radiance cube, so the loaded images themselves are
        // dropped once baked.
        EnvironmentMaps m_environment;
        std::unique_ptr<rhi::Sampler> m_sampler;
        std::string m_source;
    };
}

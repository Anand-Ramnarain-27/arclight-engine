#pragma once

#include "Math/Math.h"
#include "Renderer/Image.h"

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
        class Texture;
    }

    /// Draws a cube texture behind everything in the scene (Engine/Shaders/Skybox.hlsl).
    ///
    /// Starts with a procedural sky, a gradient from horizon to zenith with a sun where the mesh
    /// shader's light comes from; load() replaces it with six face images.
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

        /// Loads px, nx, py, ny, pz and nz (.png, .jpg, .tga or .bmp) from `folder`: square images,
        /// all the same size, laid out for a left-handed space with +Z forward, as most published
        /// skyboxes are. Keeps the current sky and logs on failure.
        bool load(Renderer& renderer, const std::string& folder);
        /// Back to the procedural sky.
        bool useProceduralSky(Renderer& renderer);

        /// Must be called inside a scene pass, after the opaque meshes.
        void draw(Renderer& renderer);

        [[nodiscard]] const std::string& source() const { return m_source; }

        bool visible = true;
        float intensity = 1.0f;

        /// The procedural sky's six faces, in the order of CubeFace, `size` pixels square, sRGB.
        [[nodiscard]] static std::array<Image, 6> proceduralSky(uint32_t size);

    private:
        bool setFaces(Renderer& renderer, const std::array<Image, 6>& faces, std::string source);

        std::array<std::unique_ptr<rhi::Pipeline>, 2> m_pipelines;   // Indexed by Camera::reversedZ.
        std::unique_ptr<rhi::Texture> m_texture;
        std::unique_ptr<rhi::Sampler> m_sampler;
        std::string m_source;
    };
}

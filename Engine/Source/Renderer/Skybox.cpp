#include "Renderer/Skybox.h"

#include "Core/Log.h"
#include "Platform/FileSystem.h"
#include "RHI/CommandList.h"
#include "RHI/Device.h"
#include "RHI/Pipeline.h"
#include "RHI/Sampler.h"
#include "RHI/Shader.h"
#include "RHI/Texture.h"
#include "RHI/TextureUtils.h"
#include "Math/Color.h"
#include "Renderer/CubeMap.h"
#include "Renderer/HdrImage.h"
#include "Renderer/Lighting.h"
#include "Renderer/Renderer.h"
#include "Renderer/ShaderLibrary.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <span>
#include <vector>

namespace arc
{
    namespace
    {
        // Mirrors SkyboxConstants in Skybox.hlsl: one 16-byte row.
        struct SkyboxConstants
        {
            float farDepth = 0.0f;
            float intensity = 1.0f;
            float rotation[2] = {1.0f, 0.0f};   // cos and sin.
        };
        static_assert(sizeof(SkyboxConstants) == 16);

        constexpr const char* kFaceNames[6] = {"px", "nx", "py", "ny", "pz", "nz"};
        constexpr const char* kExtensions[] = {".png", ".jpg", ".jpeg", ".tga", ".bmp"};
        constexpr uint32_t kProceduralSize = 128;

        std::byte encodeSrgb(float linear)
        {
            return static_cast<std::byte>(std::lround(linearToSrgb(linear) * 255.0f));
        }

        // Linear RGB of the sky in a world direction (unit length).
        Vec3 skyColor(const Vec3& direction)
        {
            const Vec3 zenith{0.10f, 0.22f, 0.52f};
            const Vec3 horizon{0.55f, 0.66f, 0.80f};
            const Vec3 ground{0.16f, 0.15f, 0.14f};

            Vec3 color;
            if (direction.y >= 0.0f)
                color = lerp(horizon, zenith, std::pow(direction.y, 0.5f));
            else
                color = lerp(horizon, ground, std::min(1.0f, -direction.y * 6.0f));

            // A disc of about 1.5 degrees, and a glow around it that fades over tens of degrees.
            static const Vec3 toSun = -defaultSunLight().direction;
            const float cosine = dot(direction, toSun);
            const float glow = std::pow(std::max(cosine, 0.0f), 48.0f) * 0.35f;
            const float disc = cosine > std::cos(toRadians(1.5f)) ? 1.0f : 0.0f;
            return color + Vec3{1.0f, 0.92f, 0.8f} * (glow + disc);
        }
    }

    Skybox::Skybox() = default;

    // Defined here, where the RHI types are complete, for the unique_ptr destructors.
    Skybox::~Skybox() = default;

    std::array<Image, 6> Skybox::proceduralSky(uint32_t size)
    {
        std::array<Image, 6> faces;
        for (uint32_t face = 0; face < 6; ++face)
        {
            Image& image = faces[face];
            image.size = {size, size};
            image.pixels.resize(size_t{size} * size * 4);
            for (uint32_t y = 0; y < size; ++y)
                for (uint32_t x = 0; x < size; ++x)
                {
                    // Texel centres, so the faces meet without a doubled edge row.
                    const float s = (static_cast<float>(x) + 0.5f) / static_cast<float>(size) * 2.0f - 1.0f;
                    const float t = (static_cast<float>(y) + 0.5f) / static_cast<float>(size) * 2.0f - 1.0f;
                    const Vec3 world = normalize(worldToCube(cubeFaceDirection(static_cast<CubeFace>(face), s, t)));
                    const Vec3 color = skyColor(world);
                    std::byte* pixel = &image.pixels[(size_t{y} * size + x) * 4];
                    pixel[0] = encodeSrgb(color.x);
                    pixel[1] = encodeSrgb(color.y);
                    pixel[2] = encodeSrgb(color.z);
                    pixel[3] = std::byte{255};
                }
        }
        return faces;
    }

    bool Skybox::init(Renderer& renderer)
    {
        const auto vertexShader = renderer.shaders().load("Skybox", rhi::ShaderStage::Vertex, "VSMain");
        const auto pixelShader = renderer.shaders().load("Skybox", rhi::ShaderStage::Pixel, "PSMain");
        if (!vertexShader || !pixelShader)
            return false;

        for (const bool reversedZ : {false, true})
        {
            Camera camera;
            camera.reversedZ = reversedZ;
            rhi::PipelineDesc desc;
            desc.vertexShader = vertexShader.get();
            desc.pixelShader = pixelShader.get();
            desc.colorFormat = Renderer::sceneFormat();
            desc.depthFormat = Renderer::sceneDepthFormat();
            // Equal passes too, so the sky at exactly the far depth covers pixels still at the clear
            // value, and nothing in front of it.
            desc.depth = {.test = true, .write = false, .compare = camera.depthCompare()};
            desc.cullMode = rhi::CullMode::None;
            desc.debugName = reversedZ ? "Skybox (Reversed-Z)" : "Skybox";
            m_pipelines[reversedZ] = renderer.device().createPipeline(desc);
            if (!m_pipelines[reversedZ])
                return false;
        }

        rhi::SamplerDesc sampler;
        sampler.addressU = sampler.addressV = sampler.addressW = rhi::AddressMode::ClampToEdge;
        sampler.debugName = "Skybox Sampler";
        m_sampler = renderer.device().createSampler(sampler);
        return m_sampler && m_environment.init(renderer.device(), renderer.shaders()) && useProceduralSky(renderer);
    }

    void Skybox::shutdown()
    {
        for (auto& pipeline : m_pipelines)
            pipeline.reset();
        m_environment.shutdown();
        m_sampler.reset();
    }

    EnvironmentLighting Skybox::environmentLighting() const
    {
        return {
            .irradiance = m_environment.irradiance(),
            .prefiltered = m_environment.prefiltered(),
            .brdfLut = m_environment.brdfLut(),
            .sampler = m_environment.sampler(),
            .prefilteredMaxLod = EnvironmentMaps::prefilteredMaxLod(),
            .intensity = intensity,
            .rotation = rotation,
        };
    }

    bool Skybox::setFaces(Renderer& renderer, const std::array<Image, 6>& faces, std::string source)
    {
        // Full mip chains, so the bake can read each face at the resolution of each radiance mip.
        const uint32_t mipLevels = rhi::fullMipCount(faces[0].size);
        auto texture = renderer.device().createTexture({
            .size = faces[0].size,
            .format = rhi::Format::RGBA8_UNORM_SRGB,
            .mipLevels = mipLevels,
            .usage = rhi::TextureUsage::Sampled,
            .type = rhi::TextureType::Cube,
            .debugName = "Sky Faces (" + source + ")",
        });
        if (!texture)
            return false;
        // Subresources go face by face, each face's mips in turn (rhi::subresourceIndex).
        std::vector<Image> levels;
        levels.reserve(size_t{mipLevels} * 6);
        for (const Image& face : faces)
            for (Image& level : generateMipChain(face, ColorSpace::Srgb))
                levels.push_back(std::move(level));
        std::vector<std::span<const std::byte>> data;
        for (const Image& level : levels)
            data.emplace_back(level.pixels);
        if (!rhi::uploadTexture(renderer.device(), *texture, data) || !m_environment.bakeFromCube(*texture, source))
            return false;
        m_source = std::move(source);
        return true;
    }

    bool Skybox::useProceduralSky(Renderer& renderer)
    {
        return setFaces(renderer, proceduralSky(kProceduralSize), "procedural");
    }

    bool Skybox::load(Renderer& renderer, const std::string& path)
    {
        std::string extension = path.size() >= 4 ? path.substr(path.size() - 4) : std::string();
        std::transform(extension.begin(), extension.end(), extension.begin(),
                       [](char c) { return static_cast<char>(std::tolower(static_cast<unsigned char>(c))); });
        return extension == ".hdr" ? loadEquirect(renderer, path) : loadFaces(renderer, path);
    }

    bool Skybox::loadEquirect(Renderer& renderer, const std::string& path)
    {
        HdrImage image = loadHdrImage(path);
        if (image.empty())
            return false;
        if (image.size.width != image.size.height * 2)
            ARC_LOG_WARN("Sky '{}' is {}x{}; an equirectangular image is twice as wide as it is tall, so it "
                            "will look stretched",
                            path, image.size.width, image.size.height);
        while (image.size.width > kMaxEquirectWidth)
            image = downsample(image);

        const std::vector<HdrImage> mips = generateMipChain(std::move(image));
        auto texture = renderer.device().createTexture({
            .size = mips[0].size,
            .format = rhi::Format::RGBA16_FLOAT,
            .mipLevels = static_cast<uint32_t>(mips.size()),
            .usage = rhi::TextureUsage::Sampled,
            .debugName = "Sky Equirect (" + path + ")",
        });
        if (!texture)
            return false;
        std::vector<std::vector<std::byte>> halves;
        halves.reserve(mips.size());
        std::vector<std::span<const std::byte>> data;
        for (const HdrImage& mip : mips)
            data.emplace_back(halves.emplace_back(toHalfFloatPixels(mip)));
        if (!rhi::uploadTexture(renderer.device(), *texture, data) || !m_environment.bakeFromEquirect(*texture, path))
            return false;
        m_source = path;
        ARC_LOG_INFO("Loaded sky '{}' ({}x{})", path, mips[0].size.width, mips[0].size.height);
        return true;
    }

    bool Skybox::loadFaces(Renderer& renderer, const std::string& folder)
    {
        std::array<Image, 6> faces;
        for (size_t face = 0; face < faces.size(); ++face)
        {
            std::string path;
            for (const char* extension : kExtensions)
            {
                const std::string candidate = folder + "/" + kFaceNames[face] + extension;
                if (fileExists(candidate))
                {
                    path = candidate;
                    break;
                }
            }
            if (path.empty())
            {
                ARC_LOG_ERROR("Skybox '{}': no {} image (.png, .jpg, .tga or .bmp)", folder, kFaceNames[face]);
                return false;
            }
            faces[face] = loadImage(path);
            if (faces[face].empty())
                return false;
            const Extent2D size = faces[face].size;
            if (size.width != size.height || size.width != faces[0].size.width || size.height != faces[0].size.height)
            {
                ARC_LOG_ERROR("Skybox '{}': faces must be square and the same size; {} is {}x{}", folder,
                              kFaceNames[face], size.width, size.height);
                return false;
            }
        }
        if (!setFaces(renderer, faces, folder))
            return false;
        ARC_LOG_INFO("Loaded skybox '{}' ({} px faces)", folder, faces[0].size.width);
        return true;
    }

    void Skybox::draw(Renderer& renderer)
    {
        const Camera& camera = renderer.sceneCamera();
        rhi::Pipeline* pipeline = m_pipelines[camera.reversedZ].get();
        rhi::Texture* radiance = m_environment.radiance();
        if (!visible || !pipeline || !radiance)
            return;

        rhi::CommandList& commandList = renderer.commandList();
        const rhi::ScopedDebugGroup group(commandList, "Skybox");
        commandList.setPipeline(*pipeline);
        commandList.setConstants(0, renderer.sceneViewConstants());
        commandList.setConstants(1, SkyboxConstants{.farDepth = camera.reversedZ ? 0.0f : 1.0f,
                                                     .intensity = intensity,
                                                     .rotation = {std::cos(rotation), std::sin(rotation)}});
        commandList.setTexture(0, *radiance);
        commandList.setSampler(0, *m_sampler);
        commandList.draw(3);
    }
}

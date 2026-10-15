#include "Scene/TextureDemoModule.h"

#include "Application/Application.h"
#include "Core/Log.h"
#include "Math/Math.h"
#include "Platform/FileSystem.h"
#include "RHI/Buffer.h"
#include "RHI/BufferUtils.h"
#include "RHI/CommandList.h"
#include "RHI/Device.h"
#include "RHI/Pipeline.h"
#include "RHI/Sampler.h"
#include "RHI/Shader.h"
#include "RHI/Texture.h"
#include "Renderer/Image.h"
#include "Renderer/ShaderConstants.h"
#include "Renderer/ShaderLibrary.h"
#include "Renderer/TextureLoader.h"

#include <imgui.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <format>
#include <span>

namespace arc
{
    namespace
    {
        // Matches VSInput in Textured.hlsl, member for member.
        struct Vertex
        {
            Vec3 position;   // Object space.
            Vec2 uv;
        };
        static_assert(sizeof(Vertex) == 20, "Vertex must be tightly packed to match the vertex layout");

        // Mirrors DrawConstants in Textured.hlsl. See ShaderConstants.h for the packing rules.
        struct TexturedDrawConstants
        {
            Mat4 world;
            Vec2 uvScale{1.0f, 1.0f};
            Vec2 uvOffset{0.0f, 0.0f};
            float mipBias = 0.0f;
            float padding[3]{};
        };
        static_assert(offsetof(TexturedDrawConstants, uvScale) == 64);
        static_assert(offsetof(TexturedDrawConstants, uvOffset) == 72);
        static_assert(offsetof(TexturedDrawConstants, mipBias) == 80);
        static_assert(sizeof(TexturedDrawConstants) == 96);

        // One unit quad in the XY plane, facing +Z, with texture coordinates from (0, 0) at the top
        // left to (1, 1). Both shapes in the demo are this quad with a different transform.
        constexpr std::array<Vertex, 4> kQuadVertices = {{
            {{-0.5f, -0.5f, 0.0f}, {0.0f, 1.0f}},
            {{0.5f, -0.5f, 0.0f}, {1.0f, 1.0f}},
            {{0.5f, 0.5f, 0.0f}, {1.0f, 0.0f}},
            {{-0.5f, 0.5f, 0.0f}, {0.0f, 0.0f}},
        }};

        // Two counter-clockwise triangles.
        constexpr std::array<uint16_t, 6> kIndices = {0, 1, 2, 0, 2, 3};

        // sRGB-encoded, so they read as named on screen.
        struct Rgb
        {
            uint8_t r, g, b;
        };
        constexpr std::array<Rgb, 10> kMipColors = {{
            {230, 60, 60},   {240, 150, 40},  {235, 220, 50}, {80, 200, 80},   {60, 200, 210},
            {70, 110, 230},  {150, 80, 220},  {230, 80, 190}, {240, 240, 240}, {128, 128, 128},
        }};

        // 40 x 80 units of ground in the y = 0 plane, from just in front of the camera into the
        // distance: the quad laid flat (its +Y edge pointing away down -Z) and scaled. The texture
        // repeats every 2 units.
        TexturedDrawConstants groundConstants(float mipBias)
        {
            TexturedDrawConstants constants;
            constants.world = Mat4::translation({0.0f, 0.0f, -40.25f}) *
                              Mat4::rotation(Quat::fromAxisAngle({1.0f, 0.0f, 0.0f}, toRadians(-90.0f))) *
                              Mat4::scaling({40.0f, 80.0f, 1.0f});
            constants.uvScale = {20.0f, 40.0f};
            constants.mipBias = mipBias;
            return constants;
        }

        // A square on screen in the top-left corner, placed directly in clip space (drawn with an
        // identity view-projection). Its texture coordinates run from -1 to 2: the centre ninth
        // shows the texture once and the rest is decided by the address mode.
        TexturedDrawConstants cornerQuadConstants(float aspect)
        {
            constexpr float kHeight = 0.5f;   // NDC units.
            constexpr float kLeft = -0.95f;
            constexpr float kTop = 0.82f;
            const float width = kHeight / aspect;

            TexturedDrawConstants constants;
            constants.world = Mat4::translation({kLeft + width * 0.5f, kTop - kHeight * 0.5f, 0.0f}) *
                              Mat4::scaling({width, kHeight, 1.0f});
            constants.uvScale = {3.0f, 3.0f};
            constants.uvOffset = {-1.0f, -1.0f};
            return constants;
        }

        Image makeChecker(Extent2D size, uint32_t cellSize, Rgb light, Rgb dark)
        {
            Image image;
            image.size = size;
            image.pixels.resize(size_t{size.width} * size.height * 4);
            for (uint32_t y = 0; y < size.height; ++y)
            {
                for (uint32_t x = 0; x < size.width; ++x)
                {
                    const Rgb color = ((x / cellSize + y / cellSize) % 2 == 0) ? light : dark;
                    std::byte* pixel = &image.pixels[(size_t{y} * size.width + x) * 4];
                    pixel[0] = std::byte{color.r};
                    pixel[1] = std::byte{color.g};
                    pixel[2] = std::byte{color.b};
                    pixel[3] = std::byte{255};
                }
            }
            return image;
        }

        // Every level is drawn by hand instead of downsampled, each in its own colour, so the
        // screen shows which level the sampler picked at every pixel.
        std::vector<Image> makeMipLevelChain(Extent2D size)
        {
            std::vector<Image> levels;
            for (uint32_t level = 0; level < rhi::fullMipCount(size); ++level)
            {
                const Rgb light = kMipColors[std::min<size_t>(level, kMipColors.size() - 1)];
                const Rgb dark = {static_cast<uint8_t>(light.r * 3 / 4), static_cast<uint8_t>(light.g * 3 / 4),
                                  static_cast<uint8_t>(light.b * 3 / 4)};
                levels.push_back(makeChecker(rhi::mipSize(size, level), 8, light, dark));
            }
            return levels;
        }

        template <typename Enum, size_t N>
        bool comboEnum(const char* label, Enum& value, const std::array<const char*, N>& names)
        {
            int index = static_cast<int>(value);
            if (!ImGui::Combo(label, &index, names.data(), static_cast<int>(N)))
                return false;
            value = static_cast<Enum>(index);
            return true;
        }

        constexpr std::array<const char*, 2> kFilterNames = {"Nearest", "Linear"};
        constexpr std::array<const char*, 4> kAddressNames = {"Repeat", "Mirrored repeat", "Clamp to edge",
                                                              "Clamp to border"};
        constexpr std::array<const char*, 3> kBorderNames = {"Transparent black", "Opaque black", "Opaque white"};
    }

    TextureDemoModule::TextureDemoModule(Application& app) : Module(app)
    {
        m_groundSamplerDesc.debugName = "Ground Sampler";
        m_quadSamplerDesc.addressU = rhi::AddressMode::ClampToBorder;
        m_quadSamplerDesc.addressV = rhi::AddressMode::ClampToBorder;
        m_quadSamplerDesc.debugName = "Address Mode Sampler";
    }

    // Defined here, where the RHI types are complete, for the unique_ptr destructors.
    TextureDemoModule::~TextureDemoModule() = default;

    bool TextureDemoModule::init()
    {
        Renderer& renderer = app().renderer();
        rhi::Device& device = renderer.device();

        const auto vertexShader = renderer.shaders().load("Textured", rhi::ShaderStage::Vertex, "VSMain");
        const auto pixelShader = renderer.shaders().load("Textured", rhi::ShaderStage::Pixel, "PSMain");
        if (!vertexShader || !pixelShader)
            return false;

        rhi::PipelineDesc pipelineDesc;
        pipelineDesc.vertexShader = vertexShader.get();
        pipelineDesc.pixelShader = pixelShader.get();
        pipelineDesc.vertexLayout = {
            .stride = sizeof(Vertex),
            .attributes = {
                {.semantic = "POSITION", .format = rhi::VertexFormat::Float3, .offset = offsetof(Vertex, position)},
                {.semantic = "TEXCOORD", .format = rhi::VertexFormat::Float2, .offset = offsetof(Vertex, uv)},
            },
        };
        pipelineDesc.colorFormat = Renderer::sceneFormat();
        pipelineDesc.debugName = "Textured";
        m_pipeline = device.createPipeline(pipelineDesc);
        if (!m_pipeline)
            return false;

        m_vertexBuffer = device.createBuffer({
            .size = sizeof(kQuadVertices),
            .usage = rhi::BufferUsage::Vertex,
            .memory = rhi::MemoryType::GpuOnly,
            .debugName = "Unit Quad Vertices",
        });
        m_indexBuffer = device.createBuffer({
            .size = sizeof(kIndices),
            .usage = rhi::BufferUsage::Index,
            .memory = rhi::MemoryType::GpuOnly,
            .debugName = "Unit Quad Indices",
        });
        if (!m_vertexBuffer || !m_indexBuffer ||
            !rhi::uploadBuffer(device, *m_vertexBuffer, std::as_bytes(std::span(kQuadVertices))) ||
            !rhi::uploadBuffer(device, *m_indexBuffer, std::as_bytes(std::span(kIndices))))
            return false;

        return createTextures() && rebuildSamplers();
    }

    bool TextureDemoModule::createTextures()
    {
        rhi::Device& device = app().renderer().device();
        // The textures being replaced may still be in use by frames in flight; the device keeps
        // their GPU memory until those frames complete.
        m_textures.clear();

        const std::string uvGridPath = findAssetPath("Textures/UVGrid.png");
        if (uvGridPath.empty())
            ARC_LOG_WARN("Assets/Textures/UVGrid.png not found; the texture demo shows generated textures only");
        else if (auto texture = loadTexture(device, uvGridPath, ColorSpace::Srgb))
            m_textures.push_back({"UV grid", std::move(texture), false});

        const std::vector<Image> checker =
            generateMipChain(makeChecker({512, 512}, 16, {235, 235, 235}, {30, 30, 30}), ColorSpace::Srgb);
        if (auto texture = createTexture(device, checker, ColorSpace::Srgb, "Checker"))
            m_textures.push_back({"Checker", std::move(texture), false});

        const std::vector<Image> mipLevels = makeMipLevelChain({512, 512});
        if (auto texture = createTexture(device, mipLevels, ColorSpace::Srgb, "Mip Levels"))
            m_textures.push_back({"Mip levels (colour per level)", std::move(texture), true});

        if (m_textures.empty())
        {
            ARC_LOG_ERROR("The texture demo could not create any texture");
            return false;
        }
        m_selectedTexture = std::min(m_selectedTexture, m_textures.size() - 1);
        return true;
    }

    bool TextureDemoModule::rebuildSamplers()
    {
        rhi::Device& device = app().renderer().device();
        // Replacing a sampler that in-flight frames still use is safe on both backends: D3D12
        // copied its descriptor into the frame's table when the draw was recorded, and Metal
        // command buffers keep the objects they reference alive.
        auto ground = device.createSampler(m_groundSamplerDesc);
        auto quad = device.createSampler(m_quadSamplerDesc);
        if (!ground || !quad)
            return false;
        m_groundSampler = std::move(ground);
        m_quadSampler = std::move(quad);
        m_samplersDirty = false;
        return true;
    }

    void TextureDemoModule::update(const FrameTime& /*time*/)
    {
        drawWindow();
        if (m_recreateTextures)
        {
            m_recreateTextures = false;
            if (!createTextures())
                app().requestQuit();
        }
        if (m_samplersDirty)
            (void)rebuildSamplers();
    }

    void TextureDemoModule::drawWindow()
    {
        ImGui::SetNextWindowSize({360.0f, 520.0f}, ImGuiCond_FirstUseEver);
        if (!ImGui::Begin("Texture Demo"))
        {
            ImGui::End();
            return;
        }

        if (ImGui::BeginCombo("Texture", m_textures[m_selectedTexture].name.c_str()))
        {
            for (size_t i = 0; i < m_textures.size(); ++i)
                if (ImGui::Selectable(m_textures[i].name.c_str(), i == m_selectedTexture))
                    m_selectedTexture = i;
            ImGui::EndCombo();
        }
        const rhi::TextureDesc& textureDesc = m_textures[m_selectedTexture].texture->desc();
        ImGui::TextDisabled("%u x %u px, %u mip levels, sRGB", textureDesc.size.width, textureDesc.size.height,
                            textureDesc.mipLevels);
        if (ImGui::Button("Recreate textures"))
            m_recreateTextures = true;
        ImGui::SetItemTooltip("Destroys and recreates every texture while frames in flight still use the old ones. "
                              "System Info shows them waiting for release.");

        ImGui::SeparatorText("Ground sampler");
        rhi::SamplerDesc& ground = m_groundSamplerDesc;
        const auto preset = [&](const char* label, rhi::Filter minMag, rhi::Filter mip, uint32_t anisotropy) {
            if (ImGui::Button(label))
            {
                ground.minFilter = ground.magFilter = minMag;
                ground.mipFilter = mip;
                ground.maxAnisotropy = anisotropy;
                m_samplersDirty = true;
            }
        };
        preset("Point", rhi::Filter::Nearest, rhi::Filter::Nearest, 1);
        ImGui::SameLine();
        preset("Bilinear", rhi::Filter::Linear, rhi::Filter::Nearest, 1);
        ImGui::SameLine();
        preset("Trilinear", rhi::Filter::Linear, rhi::Filter::Linear, 1);
        ImGui::SameLine();
        preset("Anisotropic 16x", rhi::Filter::Linear, rhi::Filter::Linear, 16);

        m_samplersDirty |= comboEnum("Min filter", ground.minFilter, kFilterNames);
        m_samplersDirty |= comboEnum("Mag filter", ground.magFilter, kFilterNames);
        m_samplersDirty |= comboEnum("Mip filter", ground.mipFilter, kFilterNames);
        int anisotropy = static_cast<int>(ground.maxAnisotropy);
        if (ImGui::SliderInt("Max anisotropy", &anisotropy, 1, 16))
        {
            ground.maxAnisotropy = static_cast<uint32_t>(anisotropy);
            m_samplersDirty = true;
        }
        if (ground.maxAnisotropy > 1)
            ImGui::TextDisabled("Anisotropic filtering overrides the three filters.");
        m_samplersDirty |= ImGui::SliderFloat("Max LOD", &ground.maxLod, 0.0f, static_cast<float>(textureDesc.mipLevels - 1),
                                              "%.1f");
        ImGui::SetItemTooltip("The least detailed mip level the sampler may use. 0 turns mipmapping off.");
        ImGui::SliderFloat("Mip bias", &m_mipBias, -4.0f, 4.0f, "%+.1f");
        ImGui::SetItemTooltip("Added to the mip level the GPU picks (a per-draw constant, b1). Negative is "
                              "sharper but aliases; positive is blurrier.");

        ImGui::SeparatorText("Camera");
        ImGui::SliderFloat("Field of view", &m_fieldOfView, 20.0f, 110.0f, "%.0f deg");
        ImGui::SliderFloat("Height", &m_cameraHeight, 0.2f, 10.0f, "%.1f");
        ImGui::TextDisabled("Sent every frame as the view constants (b0).");

        ImGui::SeparatorText("Corner quad sampler");
        ImGui::TextDisabled("Texture coordinates run from -1 to 2.");
        rhi::SamplerDesc& quad = m_quadSamplerDesc;
        if (comboEnum("Address mode", quad.addressU, kAddressNames))
        {
            quad.addressV = quad.addressU;
            m_samplersDirty = true;
        }
        if (quad.addressU == rhi::AddressMode::ClampToBorder)
            m_samplersDirty |= comboEnum("Border colour", quad.borderColor, kBorderNames);

        if (m_textures[m_selectedTexture].colorPerMipLevel)
        {
            ImGui::SeparatorText("Mip level colours");
            for (uint32_t level = 0; level < textureDesc.mipLevels; ++level)
            {
                const Rgb color = kMipColors[std::min<size_t>(level, kMipColors.size() - 1)];
                ImGui::ColorButton(std::format("Level {}", level).c_str(),
                                   {color.r / 255.0f, color.g / 255.0f, color.b / 255.0f, 1.0f},
                                   ImGuiColorEditFlags_NoTooltip, {14.0f, 14.0f});
                ImGui::SameLine();
                ImGui::Text("%u", level);
                if (level + 1 < textureDesc.mipLevels)
                    ImGui::SameLine();
            }
        }

        ImGui::End();
    }

    void TextureDemoModule::render()
    {
        Renderer& renderer = app().renderer();
        if (!renderer.isFrameActive() || !m_groundSampler || !m_quadSampler)
            return;

        // Recomputed every frame from the scene target's size, so resizing needs no extra work.
        const Extent2D size = renderer.sceneSize();
        const float aspect = static_cast<float>(size.width) / static_cast<float>(size.height);

        // Looking slightly down, so the far edge of the ground sits just below the horizon.
        ViewConstants camera;
        camera.viewProjection = Mat4::perspective(toRadians(m_fieldOfView), aspect, 0.1f, 200.0f) *
                                Mat4::lookAt({0.0f, m_cameraHeight, 0.0f}, {0.0f, m_cameraHeight - 1.0f, -6.0f},
                                             {0.0f, 1.0f, 0.0f});

        if (size.width == 0 || !renderer.beginScenePass("Texture Demo"))
            return;

        rhi::CommandList& commandList = renderer.commandList();
        commandList.setPipeline(*m_pipeline);
        commandList.setVertexBuffer(*m_vertexBuffer);
        commandList.setIndexBuffer(*m_indexBuffer, rhi::IndexFormat::UInt16);
        commandList.setTexture(0, *m_textures[m_selectedTexture].texture);
        {
            const rhi::ScopedDebugGroup group(commandList, "Ground");
            commandList.setConstants(0, camera);
            commandList.setConstants(1, groundConstants(m_mipBias));
            commandList.setSampler(0, *m_groundSampler);
            commandList.drawIndexed(static_cast<uint32_t>(kIndices.size()));
        }
        {
            const rhi::ScopedDebugGroup group(commandList, "Address Mode Quad");
            // Already in clip space, so this draw gets an identity camera of its own.
            commandList.setConstants(0, ViewConstants{});
            commandList.setConstants(1, cornerQuadConstants(aspect));
            commandList.setSampler(0, *m_quadSampler);
            commandList.drawIndexed(static_cast<uint32_t>(kIndices.size()));
        }
        renderer.endScenePass();
    }

    void TextureDemoModule::shutdown()
    {
        // The Application waits for the GPU before shutting modules down, so nothing in flight
        // still uses these.
        m_groundSampler.reset();
        m_quadSampler.reset();
        m_textures.clear();
        m_pipeline.reset();
        m_vertexBuffer.reset();
        m_indexBuffer.reset();
    }
}

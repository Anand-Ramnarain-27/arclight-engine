#include "Scene/ModelViewerModule.h"

#include "Application/Application.h"
#include "Core/Log.h"
#include "Core/Time.h"
#include "Platform/FileSystem.h"
#include "Renderer/GltfImporter.h"
#include "Renderer/Model.h"

#include <SDL3/SDL_events.h>
#include <imgui.h>

#include <algorithm>
#include <chrono>
#include <cstring>

namespace arc
{
    namespace
    {
        // Fit to view scales the model so its largest side is this many world units.
        constexpr float kFitSize = 4.0f;

        const char* alphaModeName(AlphaMode mode)
        {
            switch (mode)
            {
                case AlphaMode::Opaque: return "opaque";
                case AlphaMode::Mask:   return "mask";
                case AlphaMode::Blend:  return "blend";
            }
            return "";
        }
    }

    ModelViewerModule::ModelViewerModule(Application& app) : Module(app) {}

    // Defined here, where Model is complete, for the unique_ptr destructor.
    ModelViewerModule::~ModelViewerModule() = default;

    bool ModelViewerModule::init()
    {
        if (!m_meshRenderer.init(app().renderer()))
            return false;
        app().addEventListener(*this);

        const std::string samplePath = findAssetPath("Models/Lantern.glb");
        if (samplePath.empty())
            ARC_LOG_WARN("Assets/Models/Lantern.glb not found; re-run CMake to download it, or drop a glTF file on the window");
        else
            load(samplePath);
        return true;
    }

    void ModelViewerModule::shutdown()
    {
        app().removeEventListener(*this);
        // The Application waits for the GPU before shutting modules down.
        m_model.reset();
        m_meshRenderer.shutdown();
    }

    void ModelViewerModule::onPlatformEvent(const SDL_Event& event)
    {
        // Loaded in update(), outside the event pump and outside any frame.
        if (event.type == SDL_EVENT_DROP_FILE && event.drop.data)
            m_pendingPath = event.drop.data;
    }

    void ModelViewerModule::load(const std::string& path)
    {
        const Clock::time_point start = Clock::now();
        const std::optional<ModelData> data = importGltf(path);
        if (!data)
            return;
        std::unique_ptr<Model> model = Model::create(app().renderer().device(), *data);
        if (!model)
            return;

        // Frames in flight may still draw the old model; the device keeps its buffers and textures
        // until they finish.
        m_model = std::move(model);
        m_loadMilliseconds = std::chrono::duration<double, std::milli>(Clock::now() - start).count();
        const size_t length = std::min(path.size(), m_pathInput.size() - 1);
        std::memcpy(m_pathInput.data(), path.data(), length);
        m_pathInput[length] = '\0';
        ARC_LOG_INFO("Loaded '{}': {} instances, {} triangles, {} materials, {} textures in {:.0f} ms", m_model->name(),
                     m_model->instances().size(), m_model->triangleCount(), m_model->materials().size(),
                     m_model->textureCount(), m_loadMilliseconds);
    }

    void ModelViewerModule::update(const FrameTime& /*time*/)
    {
        if (!m_pendingPath.empty())
        {
            const std::string path = std::move(m_pendingPath);
            m_pendingPath.clear();
            load(path);
        }
        drawWindow();
    }

    Mat4 ModelViewerModule::modelTransform() const
    {
        const Mat4 rotation = Mat4::rotation(Quat::fromAxisAngle(Vec3::up(), toRadians(m_yawDegrees)));
        if (!m_fitToView || m_model->bounds().isEmpty())
            return rotation;

        // Centred on the origin, standing on y = 0, and scaled so the largest side is kFitSize.
        const Aabb& bounds = m_model->bounds();
        const Vec3 size = bounds.size();
        const float largest = std::max({size.x, size.y, size.z});
        const float scale = largest > 0.0f ? kFitSize / largest : 1.0f;
        const Vec3 pivot{bounds.center().x, bounds.min.y, bounds.center().z};
        return rotation * Mat4::scaling(Vec3{scale}) * Mat4::translation(-pivot);
    }

    void ModelViewerModule::render()
    {
        Renderer& renderer = app().renderer();
        if (!m_visible || !m_model || !renderer.isFrameActive() || !renderer.beginScenePass("Model"))
            return;
        m_meshRenderer.draw(renderer, *m_model, modelTransform(), m_debugView);
        renderer.endScenePass();
    }

    void ModelViewerModule::drawWindow()
    {
        ImGui::SetNextWindowSize({360.0f, 520.0f}, ImGuiCond_FirstUseEver);
        if (!ImGui::Begin("Model"))
        {
            ImGui::End();
            return;
        }

        ImGui::InputText("##Path", m_pathInput.data(), m_pathInput.size());
        ImGui::SameLine();
        if (ImGui::Button("Open"))
            m_pendingPath = m_pathInput.data();
        ImGui::TextDisabled("Or drop a .gltf or .glb file on the window.");

        if (!m_model)
        {
            ImGui::TextUnformatted("No model loaded.");
            ImGui::End();
            return;
        }

        ImGui::SeparatorText(m_model->name().c_str());
        ImGui::Text("%u vertices, %u triangles", m_model->vertexCount(), m_model->triangleCount());
        ImGui::Text("%zu meshes, %zu primitives, %zu instances", m_model->meshes().size(), m_model->primitives().size(),
                    m_model->instances().size());
        ImGui::Text("%zu materials, %u textures", m_model->materials().size(), m_model->textureCount());
        const Vec3 size = m_model->bounds().size();
        ImGui::TextDisabled("Size %.2f x %.2f x %.2f, loaded in %.0f ms", size.x, size.y, size.z, m_loadMilliseconds);

        ImGui::SeparatorText("Display");
        ImGui::Checkbox("Visible", &m_visible);
        ImGui::Checkbox("Fit to view", &m_fitToView);
        ImGui::SetItemTooltip("Scale to %.0f units, centred on the origin and standing on the grid. glTF is in metres, "
                              "but models come in every size.",
                              kFitSize);
        ImGui::SliderFloat("Rotation", &m_yawDegrees, -180.0f, 180.0f, "%.0f deg");
        int view = static_cast<int>(m_debugView);
        if (ImGui::Combo("Shading", &view, "Lit\0Base colour\0Normals\0Texture coordinates\0"))
            m_debugView = static_cast<MeshRenderer::DebugView>(view);

        if (ImGui::TreeNode("Instances", "Instances (%zu)", m_model->instances().size()))
        {
            for (const MeshInstance& instance : m_model->instances())
            {
                const MeshData& mesh = m_model->meshes()[instance.mesh];
                ImGui::BulletText("%s: mesh '%s', %u primitives", instance.name.empty() ? "(unnamed)" : instance.name.c_str(),
                                  mesh.name.c_str(), mesh.primitiveCount);
            }
            ImGui::TreePop();
        }

        if (ImGui::TreeNode("Materials", "Materials (%zu)", m_model->materials().size()))
        {
            for (const MaterialData& material : m_model->materials())
            {
                const Vec4& c = material.baseColorFactor;
                ImGui::ColorButton(material.name.c_str(), {c.x, c.y, c.z, c.w},
                                   ImGuiColorEditFlags_NoTooltip | ImGuiColorEditFlags_AlphaPreviewHalf, {14.0f, 14.0f});
                ImGui::SameLine();
                const char* texture = material.baseColorTexture < 0                         ? "no texture"
                                      : m_model->texture(material.baseColorTexture) != nullptr ? "texture"
                                                                                               : "texture missing";
                ImGui::Text("%s: %s, %s%s", material.name.empty() ? "(unnamed)" : material.name.c_str(), texture,
                            alphaModeName(material.alphaMode), material.doubleSided ? ", double-sided" : "");
            }
            ImGui::TreePop();
        }

        ImGui::End();
    }
}

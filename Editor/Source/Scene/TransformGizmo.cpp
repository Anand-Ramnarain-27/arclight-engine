#include "Scene/TransformGizmo.h"

#include "Renderer/Camera.h"
#include "Scene/GameObject.h"

#include <imgui.h>
// After imgui.h, which ImGuizmo.h expects to have been included.
#include <ImGuizmo.h>

#include <algorithm>
#include <array>
#include <cstring>

namespace arc
{
    namespace
    {
        std::array<float, 16> toFloats(const Mat4& m)
        {
            std::array<float, 16> floats{};
            std::memcpy(floats.data(), m.data(), sizeof(floats));
            return floats;
        }

        Mat4 fromFloats(const std::array<float, 16>& f)
        {
            return {{f[0], f[1], f[2], f[3]}, {f[4], f[5], f[6], f[7]}, {f[8], f[9], f[10], f[11]}, {f[12], f[13], f[14], f[15]}};
        }

        ImGuizmo::OPERATION toImGuizmo(TransformGizmo::Operation operation)
        {
            switch (operation)
            {
                case TransformGizmo::Operation::Translate: return ImGuizmo::TRANSLATE;
                case TransformGizmo::Operation::Rotate:    return ImGuizmo::ROTATE;
                case TransformGizmo::Operation::Scale:     return ImGuizmo::SCALE;
            }
            return ImGuizmo::TRANSLATE;
        }
    }

    void TransformGizmo::beginFrame()
    {
        ImGuizmo::BeginFrame();
    }

    bool TransformGizmo::isActive()
    {
        return ImGuizmo::IsOver() || ImGuizmo::IsUsing();
    }

    bool TransformGizmo::draw(GameObject& object, const Camera& camera, const ImVec2& viewMin, const ImVec2& viewSize)
    {
        if (viewSize.x <= 0.0f || viewSize.y <= 0.0f)
            return false;

        // ImGuizmo only projects points with these matrices, and its checks for points behind the
        // camera assume a standard projection with a finite far plane. The camera's reversed-Z,
        // infinite projection maps x and y the same way, so a standard one with the same field of
        // view and aspect ratio draws the handles exactly where they belong.
        const float aspect = viewSize.x / viewSize.y;
        const float farZ = std::max(camera.farZ, camera.nearZ * 2.0f);
        const std::array<float, 16> view = toFloats(camera.view());
        const std::array<float, 16> projection = toFloats(Mat4::perspective(camera.verticalFov, aspect, camera.nearZ, farZ));

        // Column-major matrices with column vectors have the same memory layout as ImGuizmo's
        // row-major matrices with row vectors, so they pass through unchanged.
        std::array<float, 16> world = toFloats(object.transform().worldMatrix());

        ImGuizmo::SetOrthographic(false);
        ImGuizmo::SetDrawlist(ImGui::GetWindowDrawList());
        ImGuizmo::SetRect(viewMin.x, viewMin.y, viewSize.x, viewSize.y);

        const bool snapping = snap != ImGui::GetIO().KeyCtrl;
        const float snapValue = operation == Operation::Translate ? translateSnap
                                : operation == Operation::Rotate  ? rotateSnap
                                                                  : scaleSnap;
        const std::array<float, 3> snapValues = {snapValue, snapValue, snapValue};
        // Scale handles always follow the object's own axes: a world-aligned scale of a rotated
        // object would be a shear, which a transform cannot hold.
        const ImGuizmo::MODE mode = space == Space::Local || operation == Operation::Scale ? ImGuizmo::LOCAL : ImGuizmo::WORLD;

        if (!ImGuizmo::Manipulate(view.data(), projection.data(), toImGuizmo(operation), mode, world.data(), nullptr,
                                  snapping ? snapValues.data() : nullptr))
            return false;
        return object.transform().setWorldMatrix(fromFloats(world));
    }
}

#include "Scene/Raycast.h"

#include "Renderer/Model.h"
#include "Scene/MeshComponent.h"
#include "Scene/Scene.h"

#include <cmath>

namespace arc
{
    namespace
    {
        // Updates `best` if a triangle of the mesh is hit nearer than it. `local` is the ray in the
        // mesh's object space, whose t values equal the world ray's.
        void raycastMesh(const MeshComponent& mesh, const Ray& local, std::optional<RaycastHit>& best, GameObject& object)
        {
            const Model& model = *mesh.model();
            const MeshData& data = model.meshes()[mesh.meshIndex()];
            const std::vector<Vec3>& positions = model.positions();
            const std::vector<uint32_t>& indices = model.indices();

            for (uint32_t p = 0; p < data.primitiveCount; ++p)
            {
                const Primitive& primitive = model.primitives()[data.firstPrimitive + p];
                const std::optional<float> boundsHit = intersectRayAabb(local, primitive.bounds);
                if (!boundsHit || (best && *boundsHit >= best->distance))
                    continue;

                for (uint32_t i = 0; i + 2 < primitive.indexCount; i += 3)
                {
                    const uint32_t first = primitive.firstIndex + i;
                    const Vec3& a = positions[primitive.baseVertex + indices[first]];
                    const Vec3& b = positions[primitive.baseVertex + indices[first + 1]];
                    const Vec3& c = positions[primitive.baseVertex + indices[first + 2]];
                    const std::optional<float> t = intersectRayTriangle(local, a, b, c);
                    if (t && (!best || *t < best->distance))
                        best = RaycastHit{.object = &object, .mesh = &mesh, .distance = *t, .point = {}, .triangle = first / 3};
                }
            }
        }
    }

    std::optional<RaycastHit> raycast(const Scene& scene, const Ray& ray)
    {
        std::optional<RaycastHit> best;
        scene.forEachActive([&](GameObject& object) {
            for (const std::unique_ptr<Component>& component : object.components())
            {
                if (!component->enabled || &component->type() != &MeshComponent::staticType())
                    continue;
                const auto& mesh = static_cast<const MeshComponent&>(*component);
                if (!mesh.hasMesh())
                    continue;

                const Mat4& world = object.transform().worldMatrix();
                const std::optional<float> boundsHit = intersectRayAabb(ray, mesh.localBounds().transformed(world));
                if (!boundsHit || (best && *boundsHit >= best->distance))
                    continue;
                // A zero scale (a hidden node) has no inverse and nothing visible to hit.
                if (std::abs(determinant(world)) <= 1e-12f)
                    continue;
                raycastMesh(mesh, ray.transformed(inverse(world)), best, object);
            }
        });
        if (best)
            best->point = ray.at(best->distance);
        return best;
    }
}

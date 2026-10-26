#include "Renderer/TangentGenerator.h"

#include <meshoptimizer.h>
#include <mikktspace.h>

#include <cmath>

namespace arc
{
    namespace
    {
        // MikkTSpace asks for each corner of each triangle; the triangles are unindexed here so
        // every corner can take its own tangent, and are merged again afterwards.
        struct Corners
        {
            std::vector<MeshVertex>& vertices;
        };

        MeshVertex& corner(const SMikkTSpaceContext* context, int face, int vertex)
        {
            auto& corners = *static_cast<Corners*>(context->m_pUserData);
            return corners.vertices[static_cast<size_t>(face) * 3 + static_cast<size_t>(vertex)];
        }

        Vec3 perpendicular(const Vec3& normal)
        {
            const Vec3 helper = std::abs(normal.y) < 0.99f ? Vec3::up() : Vec3::right();
            return normalize(cross(helper, normal));
        }
    }

    void generateTangents(std::vector<MeshVertex>& vertices, std::vector<uint32_t>& indices)
    {
        if (indices.size() < 3)
        {
            fillDefaultTangents(vertices);
            return;
        }

        std::vector<MeshVertex> soup;
        soup.reserve(indices.size());
        for (const uint32_t index : indices)
            soup.push_back(vertices[index]);
        // Degenerate triangles get no tangent from MikkTSpace; they keep this one.
        fillDefaultTangents(soup);

        Corners corners{soup};
        SMikkTSpaceInterface callbacks{};
        callbacks.m_getNumFaces = [](const SMikkTSpaceContext* context) {
            return static_cast<int>(static_cast<Corners*>(context->m_pUserData)->vertices.size() / 3);
        };
        callbacks.m_getNumVerticesOfFace = [](const SMikkTSpaceContext*, int) { return 3; };
        callbacks.m_getPosition = [](const SMikkTSpaceContext* context, float out[], int face, int vertex) {
            const Vec3& p = corner(context, face, vertex).position;
            out[0] = p.x;
            out[1] = p.y;
            out[2] = p.z;
        };
        callbacks.m_getNormal = [](const SMikkTSpaceContext* context, float out[], int face, int vertex) {
            const Vec3& n = corner(context, face, vertex).normal;
            out[0] = n.x;
            out[1] = n.y;
            out[2] = n.z;
        };
        // MikkTSpace follows OpenGL, where v grows up the image; glTF's v grows down it. Flipping v
        // here makes the bitangent point up the image, where a normal map's green channel points, so
        // the sign comes out as glTF defines it.
        callbacks.m_getTexCoord = [](const SMikkTSpaceContext* context, float out[], int face, int vertex) {
            const Vec2& uv = corner(context, face, vertex).uv;
            out[0] = uv.x;
            out[1] = 1.0f - uv.y;
        };
        callbacks.m_setTSpaceBasic = [](const SMikkTSpaceContext* context, const float tangent[], float sign, int face,
                                        int vertex) {
            corner(context, face, vertex).tangent = {tangent[0], tangent[1], tangent[2], sign < 0.0f ? -1.0f : 1.0f};
        };

        SMikkTSpaceContext context{};
        context.m_pInterface = &callbacks;
        context.m_pUserData = &corners;
        genTangSpaceDefault(&context);

        // Corners that ended up with the same position, normal, coordinates and tangent become one
        // vertex again.
        std::vector<uint32_t> remap(soup.size());
        const size_t unique =
            meshopt_generateVertexRemap(remap.data(), nullptr, soup.size(), soup.data(), soup.size(), sizeof(MeshVertex));
        vertices.resize(unique);
        meshopt_remapVertexBuffer(vertices.data(), soup.data(), soup.size(), sizeof(MeshVertex), remap.data());
        indices = std::move(remap);
    }

    void fillDefaultTangents(std::span<MeshVertex> vertices)
    {
        for (MeshVertex& vertex : vertices)
        {
            const Vec3 t = perpendicular(vertex.normal);
            vertex.tangent = {t.x, t.y, t.z, 1.0f};
        }
    }
}

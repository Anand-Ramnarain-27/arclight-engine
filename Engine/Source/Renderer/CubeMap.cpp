#include "Renderer/CubeMap.h"

#include <cmath>

namespace arc
{
    // The face selection table of the D3D12 and Vulkan specifications: for each face the major axis,
    // and how the other two components map to the face's s (right) and t (down) coordinates.

    Vec3 cubeFaceDirection(CubeFace face, float s, float t)
    {
        switch (face)
        {
            case CubeFace::PositiveX: return {1.0f, -t, -s};
            case CubeFace::NegativeX: return {-1.0f, -t, s};
            case CubeFace::PositiveY: return {s, 1.0f, t};
            case CubeFace::NegativeY: return {s, -1.0f, -t};
            case CubeFace::PositiveZ: return {s, -t, 1.0f};
            case CubeFace::NegativeZ: return {-s, -t, -1.0f};
        }
        return {};
    }

    CubeFacePoint cubeFacePoint(const Vec3& d)
    {
        const float ax = std::abs(d.x);
        const float ay = std::abs(d.y);
        const float az = std::abs(d.z);
        if (ax >= ay && ax >= az)
            return d.x > 0.0f ? CubeFacePoint{CubeFace::PositiveX, -d.z / ax, -d.y / ax}
                              : CubeFacePoint{CubeFace::NegativeX, d.z / ax, -d.y / ax};
        if (ay >= az)
            return d.y > 0.0f ? CubeFacePoint{CubeFace::PositiveY, d.x / ay, d.z / ay}
                              : CubeFacePoint{CubeFace::NegativeY, d.x / ay, -d.z / ay};
        return d.z > 0.0f ? CubeFacePoint{CubeFace::PositiveZ, d.x / az, -d.y / az}
                          : CubeFacePoint{CubeFace::NegativeZ, -d.x / az, -d.y / az};
    }
}

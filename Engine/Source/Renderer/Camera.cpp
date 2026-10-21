#include "Renderer/Camera.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace arc
{
    void Camera::lookAlong(const Vec3& direction)
    {
        const Vec3 d = normalize(direction);
        if (lengthSquared(d) == 0.0f)
            return;
        // forward = (-sin(yaw) cos(pitch), sin(pitch), -cos(yaw) cos(pitch)); see rotation().
        pitch = std::clamp(std::asin(std::clamp(d.y, -1.0f, 1.0f)), -kMaxPitch, kMaxPitch);
        if (d.x != 0.0f || d.z != 0.0f)
            yaw = std::atan2(-d.x, -d.z);
    }

    Mat4 Camera::view() const
    {
        // The camera's transform is translation(position) * rotation; its inverse undoes the
        // translation first and then the rotation. A unit quaternion's inverse is its conjugate.
        return Mat4::rotation(conjugate(rotation())) * Mat4::translation(-position);
    }

    Mat4 Camera::projection(float aspect) const
    {
        return reversedZ ? Mat4::perspectiveReversedZ(verticalFov, aspect, nearZ)
                         : Mat4::perspective(verticalFov, aspect, nearZ, farZ);
    }

    Ray Camera::viewportRay(Vec2 ndc, float aspect) const
    {
        // The projection's inverse for a direction: at view depth 1 the image spans tan(fov / 2)
        // up and aspect times that across, whatever the near plane or depth convention.
        const float halfHeight = std::tan(verticalFov * 0.5f);
        const Vec3 direction = forward() + right() * (ndc.x * halfHeight * aspect) + up() * (ndc.y * halfHeight);
        return {position, normalize(direction)};
    }

    Frustum Camera::frustum(float aspect) const
    {
        const float far = reversedZ ? std::numeric_limits<float>::infinity() : farZ;
        return Frustum::fromCamera(position, forward(), right(), up(), verticalFov, aspect, nearZ, far);
    }
}

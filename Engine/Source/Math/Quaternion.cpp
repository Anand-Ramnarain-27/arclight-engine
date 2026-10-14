#include "Math/Quaternion.h"
#include "Math/Matrix.h"

#include <cmath>

namespace arc
{
    Quat Quat::fromAxisAngle(const Vec3& axis, float radians)
    {
        const Vec3 n = normalize(axis);
        const float s = std::sin(radians * 0.5f);
        return {n.x * s, n.y * s, n.z * s, std::cos(radians * 0.5f)};
    }

    Quat Quat::fromEuler(const Vec3& pitchYawRoll)
    {
        const Quat yaw = fromAxisAngle(Vec3::up(), pitchYawRoll.y);
        const Quat pitch = fromAxisAngle(Vec3::right(), pitchYawRoll.x);
        const Quat roll = fromAxisAngle({0.0f, 0.0f, 1.0f}, pitchYawRoll.z);
        return yaw * pitch * roll;
    }

    Vec3 Quat::toEuler() const
    {
        // For R = Ry(yaw) * Rx(pitch) * Rz(roll): R(1,2) = -sin(pitch),
        // R(0,2) / R(2,2) = tan(yaw) and R(1,0) / R(1,1) = tan(roll).
        const Mat4 r = Mat4::rotation(*this);
        const float sinPitch = std::clamp(-r(1, 2), -1.0f, 1.0f);
        const float pitch = std::asin(sinPitch);

        if (std::abs(sinPitch) > 1.0f - kEpsilon)
        {
            // Gimbal lock: yaw and roll rotate about the same axis, so fold everything into yaw.
            return {pitch, std::atan2(-r(2, 0), r(0, 0)), 0.0f};
        }

        return {pitch, std::atan2(r(0, 2), r(2, 2)), std::atan2(r(1, 0), r(1, 1))};
    }

    Quat normalize(const Quat& q)
    {
        const float len = std::sqrt(dot(q, q));
        if (len <= 0.0f)
            return Quat::identity();
        return {q.x / len, q.y / len, q.z / len, q.w / len};
    }

    Quat slerp(const Quat& a, const Quat& b, float t)
    {
        Quat end = b;
        float cosTheta = dot(a, b);

        // q and -q are the same rotation; flipping takes the short way round.
        if (cosTheta < 0.0f)
        {
            end = {-b.x, -b.y, -b.z, -b.w};
            cosTheta = -cosTheta;
        }

        // Nearly parallel: sin(theta) approaches zero, so fall back to normalised lerp.
        if (cosTheta > 1.0f - kEpsilon)
        {
            return normalize({lerp(a.x, end.x, t), lerp(a.y, end.y, t), lerp(a.z, end.z, t), lerp(a.w, end.w, t)});
        }

        const float theta = std::acos(cosTheta);
        const float sinTheta = std::sin(theta);
        const float wa = std::sin((1.0f - t) * theta) / sinTheta;
        const float wb = std::sin(t * theta) / sinTheta;
        return {a.x * wa + end.x * wb, a.y * wa + end.y * wb, a.z * wa + end.z * wb, a.w * wa + end.w * wb};
    }

    bool nearlyEqualRotation(const Quat& a, const Quat& b, float epsilon)
    {
        return std::abs(std::abs(dot(a, b)) - 1.0f) <= epsilon;
    }
}

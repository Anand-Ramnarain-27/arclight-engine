#pragma once

#include "Math/MathUtils.h"

#include <cmath>

namespace arc
{
    struct Vec2
    {
        float x = 0.0f;
        float y = 0.0f;

        constexpr Vec2() = default;
        constexpr Vec2(float x_, float y_) : x(x_), y(y_) {}
        constexpr explicit Vec2(float s) : x(s), y(s) {}

        constexpr Vec2 operator-() const { return {-x, -y}; }
        constexpr Vec2 operator+(Vec2 o) const { return {x + o.x, y + o.y}; }
        constexpr Vec2 operator-(Vec2 o) const { return {x - o.x, y - o.y}; }
        constexpr Vec2 operator*(Vec2 o) const { return {x * o.x, y * o.y}; }
        constexpr Vec2 operator*(float s) const { return {x * s, y * s}; }
        constexpr Vec2 operator/(float s) const { return {x / s, y / s}; }
        constexpr Vec2& operator+=(Vec2 o) { x += o.x; y += o.y; return *this; }
        constexpr Vec2& operator-=(Vec2 o) { x -= o.x; y -= o.y; return *this; }
        constexpr Vec2& operator*=(float s) { x *= s; y *= s; return *this; }
        constexpr bool operator==(const Vec2&) const = default;
    };

    struct Vec3
    {
        float x = 0.0f;
        float y = 0.0f;
        float z = 0.0f;

        constexpr Vec3() = default;
        constexpr Vec3(float x_, float y_, float z_) : x(x_), y(y_), z(z_) {}
        constexpr explicit Vec3(float s) : x(s), y(s), z(s) {}

        constexpr Vec3 operator-() const { return {-x, -y, -z}; }
        constexpr Vec3 operator+(const Vec3& o) const { return {x + o.x, y + o.y, z + o.z}; }
        constexpr Vec3 operator-(const Vec3& o) const { return {x - o.x, y - o.y, z - o.z}; }
        constexpr Vec3 operator*(const Vec3& o) const { return {x * o.x, y * o.y, z * o.z}; }
        constexpr Vec3 operator*(float s) const { return {x * s, y * s, z * s}; }
        constexpr Vec3 operator/(float s) const { return {x / s, y / s, z / s}; }
        constexpr Vec3& operator+=(const Vec3& o) { x += o.x; y += o.y; z += o.z; return *this; }
        constexpr Vec3& operator-=(const Vec3& o) { x -= o.x; y -= o.y; z -= o.z; return *this; }
        constexpr Vec3& operator*=(float s) { x *= s; y *= s; z *= s; return *this; }
        constexpr bool operator==(const Vec3&) const = default;

        static constexpr Vec3 zero() { return {0.0f, 0.0f, 0.0f}; }
        static constexpr Vec3 one() { return {1.0f, 1.0f, 1.0f}; }
        static constexpr Vec3 right() { return {1.0f, 0.0f, 0.0f}; }
        static constexpr Vec3 up() { return {0.0f, 1.0f, 0.0f}; }
        /// -Z: the direction an unrotated camera or object faces.
        static constexpr Vec3 forward() { return {0.0f, 0.0f, -1.0f}; }
    };

    struct Vec4
    {
        float x = 0.0f;
        float y = 0.0f;
        float z = 0.0f;
        float w = 0.0f;

        constexpr Vec4() = default;
        constexpr Vec4(float x_, float y_, float z_, float w_) : x(x_), y(y_), z(z_), w(w_) {}
        constexpr Vec4(const Vec3& v, float w_) : x(v.x), y(v.y), z(v.z), w(w_) {}
        constexpr explicit Vec4(float s) : x(s), y(s), z(s), w(s) {}

        [[nodiscard]] constexpr Vec3 xyz() const { return {x, y, z}; }

        constexpr float& operator[](int i) { return i == 0 ? x : i == 1 ? y : i == 2 ? z : w; }
        constexpr float operator[](int i) const { return i == 0 ? x : i == 1 ? y : i == 2 ? z : w; }

        constexpr Vec4 operator-() const { return {-x, -y, -z, -w}; }
        constexpr Vec4 operator+(const Vec4& o) const { return {x + o.x, y + o.y, z + o.z, w + o.w}; }
        constexpr Vec4 operator-(const Vec4& o) const { return {x - o.x, y - o.y, z - o.z, w - o.w}; }
        constexpr Vec4 operator*(const Vec4& o) const { return {x * o.x, y * o.y, z * o.z, w * o.w}; }
        constexpr Vec4 operator*(float s) const { return {x * s, y * s, z * s, w * s}; }
        constexpr Vec4 operator/(float s) const { return {x / s, y / s, z / s, w / s}; }
        constexpr Vec4& operator+=(const Vec4& o) { x += o.x; y += o.y; z += o.z; w += o.w; return *this; }
        constexpr bool operator==(const Vec4&) const = default;
    };

    constexpr Vec2 operator*(float s, Vec2 v) { return v * s; }
    constexpr Vec3 operator*(float s, const Vec3& v) { return v * s; }
    constexpr Vec4 operator*(float s, const Vec4& v) { return v * s; }

    [[nodiscard]] constexpr float dot(Vec2 a, Vec2 b) { return a.x * b.x + a.y * b.y; }
    [[nodiscard]] constexpr float dot(const Vec3& a, const Vec3& b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
    [[nodiscard]] constexpr float dot(const Vec4& a, const Vec4& b) { return a.x * b.x + a.y * b.y + a.z * b.z + a.w * b.w; }

    /// Right-handed: cross(right, up) == -forward == +Z.
    [[nodiscard]] constexpr Vec3 cross(const Vec3& a, const Vec3& b)
    {
        return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
    }

    [[nodiscard]] constexpr float lengthSquared(Vec2 v) { return dot(v, v); }
    [[nodiscard]] constexpr float lengthSquared(const Vec3& v) { return dot(v, v); }
    [[nodiscard]] constexpr float lengthSquared(const Vec4& v) { return dot(v, v); }

    [[nodiscard]] inline float length(Vec2 v) { return std::sqrt(lengthSquared(v)); }
    [[nodiscard]] inline float length(const Vec3& v) { return std::sqrt(lengthSquared(v)); }
    [[nodiscard]] inline float length(const Vec4& v) { return std::sqrt(lengthSquared(v)); }

    [[nodiscard]] inline float distance(const Vec3& a, const Vec3& b) { return length(a - b); }

    /// Returns zero for a zero-length input instead of producing NaNs.
    [[nodiscard]] inline Vec2 normalize(Vec2 v)
    {
        const float len = length(v);
        return len > 0.0f ? v / len : Vec2{};
    }

    /// Returns zero for a zero-length input instead of producing NaNs.
    [[nodiscard]] inline Vec3 normalize(const Vec3& v)
    {
        const float len = length(v);
        return len > 0.0f ? v / len : Vec3{};
    }

    [[nodiscard]] constexpr Vec3 lerp(const Vec3& a, const Vec3& b, float t) { return a + (b - a) * t; }

    [[nodiscard]] inline bool nearlyEqual(const Vec3& a, const Vec3& b, float epsilon = kEpsilon)
    {
        return nearlyEqual(a.x, b.x, epsilon) && nearlyEqual(a.y, b.y, epsilon) && nearlyEqual(a.z, b.z, epsilon);
    }

    [[nodiscard]] inline bool nearlyEqual(const Vec4& a, const Vec4& b, float epsilon = kEpsilon)
    {
        return nearlyEqual(a.xyz(), b.xyz(), epsilon) && nearlyEqual(a.w, b.w, epsilon);
    }
}

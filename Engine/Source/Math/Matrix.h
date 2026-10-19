#pragma once

#include "Math/Quaternion.h"
#include "Math/Vector.h"

#include <optional>

namespace arc
{
    /// 4x4 float matrix, column-major, used with column vectors (v' = M * v).
    class Mat4
    {
    public:
        /// Identity.
        constexpr Mat4()
            : m_columns{{1.0f, 0.0f, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 1.0f, 0.0f}, {0.0f, 0.0f, 0.0f, 1.0f}}
        {
        }

        constexpr Mat4(const Vec4& c0, const Vec4& c1, const Vec4& c2, const Vec4& c3)
            : m_columns{c0, c1, c2, c3}
        {
        }

        static constexpr Mat4 identity() { return {}; }

        [[nodiscard]] constexpr const Vec4& column(int index) const { return m_columns[index]; }
        constexpr Vec4& column(int index) { return m_columns[index]; }

        /// Mathematical (row, column) indexing, independent of storage order.
        [[nodiscard]] constexpr float operator()(int row, int col) const { return m_columns[col][row]; }
        constexpr float& operator()(int row, int col) { return m_columns[col][row]; }

        /// Pointer to 16 floats in column-major order, for uploading to the GPU.
        [[nodiscard]] const float* data() const { return &m_columns[0].x; }

        constexpr Vec4 operator*(const Vec4& v) const
        {
            return m_columns[0] * v.x + m_columns[1] * v.y + m_columns[2] * v.z + m_columns[3] * v.w;
        }

        constexpr Mat4 operator*(const Mat4& o) const
        {
            return {*this * o.m_columns[0], *this * o.m_columns[1], *this * o.m_columns[2], *this * o.m_columns[3]};
        }

        constexpr bool operator==(const Mat4&) const = default;

        /// Transforms a position (w = 1); translation applies.
        [[nodiscard]] constexpr Vec3 transformPoint(const Vec3& p) const { return (*this * Vec4(p, 1.0f)).xyz(); }

        /// Transforms a direction (w = 0); translation is ignored. Not renormalised.
        [[nodiscard]] constexpr Vec3 transformVector(const Vec3& v) const { return (*this * Vec4(v, 0.0f)).xyz(); }

        [[nodiscard]] constexpr Vec3 translationPart() const { return m_columns[3].xyz(); }

        [[nodiscard]] static constexpr Mat4 translation(const Vec3& t)
        {
            return {{1.0f, 0.0f, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 1.0f, 0.0f}, {t, 1.0f}};
        }

        [[nodiscard]] static constexpr Mat4 scaling(const Vec3& s)
        {
            return {{s.x, 0.0f, 0.0f, 0.0f}, {0.0f, s.y, 0.0f, 0.0f}, {0.0f, 0.0f, s.z, 0.0f}, {0.0f, 0.0f, 0.0f, 1.0f}};
        }

        /// `q` must be normalised.
        [[nodiscard]] static constexpr Mat4 rotation(const Quat& q)
        {
            const float xx = q.x * q.x, yy = q.y * q.y, zz = q.z * q.z;
            const float xy = q.x * q.y, xz = q.x * q.z, yz = q.y * q.z;
            const float wx = q.w * q.x, wy = q.w * q.y, wz = q.w * q.z;
            return {
                {1.0f - 2.0f * (yy + zz), 2.0f * (xy + wz), 2.0f * (xz - wy), 0.0f},
                {2.0f * (xy - wz), 1.0f - 2.0f * (xx + zz), 2.0f * (yz + wx), 0.0f},
                {2.0f * (xz + wy), 2.0f * (yz - wx), 1.0f - 2.0f * (xx + yy), 0.0f},
                {0.0f, 0.0f, 0.0f, 1.0f},
            };
        }

        /// translation * rotation * scale: scales first, then rotates, then translates.
        [[nodiscard]] static constexpr Mat4 trs(const Vec3& t, const Quat& r, const Vec3& s)
        {
            Mat4 m = rotation(r);
            m.m_columns[0] = m.m_columns[0] * s.x;
            m.m_columns[1] = m.m_columns[1] * s.y;
            m.m_columns[2] = m.m_columns[2] * s.z;
            m.m_columns[3] = {t, 1.0f};
            return m;
        }

        /// View matrix for a camera at `eye` looking at `target`. The camera looks down its local -Z.
        [[nodiscard]] static Mat4 lookAt(const Vec3& eye, const Vec3& target, const Vec3& up);

        /// Perspective projection mapping view depth [-near, -far] to NDC depth [0, 1].
        /// `fovY` in radians; `aspect` is width / height.
        [[nodiscard]] static Mat4 perspective(float fovY, float aspect, float nearZ, float farZ);

        /// Reversed-Z perspective with an infinite far plane: depth 1 at the near plane, approaching 0
        /// at infinity. Requires a GREATER depth test and clearing depth to 0.
        [[nodiscard]] static Mat4 perspectiveReversedZ(float fovY, float aspect, float nearZ);

        /// Orthographic projection mapping view depth [-near, -far] to NDC depth [0, 1].
        [[nodiscard]] static Mat4 orthographic(float left, float right, float bottom, float top, float nearZ, float farZ);

    private:
        Vec4 m_columns[4];
    };

    [[nodiscard]] Mat4 transpose(const Mat4& m);

    [[nodiscard]] float determinant(const Mat4& m);

    /// General inverse. Returns identity and asserts in debug if `m` is singular.
    [[nodiscard]] Mat4 inverse(const Mat4& m);

    /// Transforms normals correctly under non-uniform scale: the inverse transpose of the
    /// upper 3x3, with translation removed.
    [[nodiscard]] Mat4 normalMatrix(const Mat4& model);

    [[nodiscard]] bool nearlyEqual(const Mat4& a, const Mat4& b, float epsilon = kEpsilon);

    /// A transform split into translation * rotation * scale, the inverse of Mat4::trs().
    struct Decomposed
    {
        Vec3 translation;
        Quat rotation;
        Vec3 scale = Vec3::one();
    };

    /// Splits an affine matrix into translation, rotation and scale. A mirroring matrix (negative
    /// determinant) gets a negative X scale. Shear cannot be represented and is lost: the result is
    /// exact only for matrices built from TRS products where every scale is uniform or aligned with
    /// the rotation that follows it. Returns nothing if a scale axis is zero.
    [[nodiscard]] std::optional<Decomposed> decompose(const Mat4& m);
}

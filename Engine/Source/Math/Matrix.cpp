#include "Math/Matrix.h"

#include "Core/Assert.h"

#include <cmath>

namespace arc
{
    Mat4 Mat4::lookAt(const Vec3& eye, const Vec3& target, const Vec3& up)
    {
        const Vec3 forward = normalize(target - eye);
        const Vec3 right = normalize(cross(forward, up));
        ARC_ASSERT(lengthSquared(right) > 0.0f, "lookAt: `up` is parallel to the view direction");
        const Vec3 cameraUp = cross(right, forward);

        // Rows are the camera basis, so this is the inverse of the camera's world rotation;
        // the last column is the inverse translation expressed in camera space.
        Mat4 m;
        m(0, 0) = right.x;    m(0, 1) = right.y;    m(0, 2) = right.z;    m(0, 3) = -dot(right, eye);
        m(1, 0) = cameraUp.x; m(1, 1) = cameraUp.y; m(1, 2) = cameraUp.z; m(1, 3) = -dot(cameraUp, eye);
        m(2, 0) = -forward.x; m(2, 1) = -forward.y; m(2, 2) = -forward.z; m(2, 3) = dot(forward, eye);
        return m;
    }

    Mat4 Mat4::perspective(float fovY, float aspect, float nearZ, float farZ)
    {
        ARC_ASSERT(nearZ > 0.0f && farZ > nearZ && aspect > 0.0f);
        const float f = 1.0f / std::tan(fovY * 0.5f);

        Mat4 m;
        m(0, 0) = f / aspect;
        m(1, 1) = f;
        m(2, 2) = farZ / (nearZ - farZ);
        m(2, 3) = -(farZ * nearZ) / (farZ - nearZ);
        m(3, 2) = -1.0f;
        m(3, 3) = 0.0f;
        return m;
    }

    Mat4 Mat4::perspectiveReversedZ(float fovY, float aspect, float nearZ)
    {
        ARC_ASSERT(nearZ > 0.0f && aspect > 0.0f);
        const float f = 1.0f / std::tan(fovY * 0.5f);

        Mat4 m;
        m(0, 0) = f / aspect;
        m(1, 1) = f;
        m(2, 2) = 0.0f;
        m(2, 3) = nearZ;
        m(3, 2) = -1.0f;
        m(3, 3) = 0.0f;
        return m;
    }

    Mat4 Mat4::orthographic(float left, float right, float bottom, float top, float nearZ, float farZ)
    {
        ARC_ASSERT(right != left && top != bottom && farZ != nearZ);

        Mat4 m;
        m(0, 0) = 2.0f / (right - left);
        m(1, 1) = 2.0f / (top - bottom);
        m(2, 2) = -1.0f / (farZ - nearZ);
        m(0, 3) = -(right + left) / (right - left);
        m(1, 3) = -(top + bottom) / (top - bottom);
        m(2, 3) = -nearZ / (farZ - nearZ);
        return m;
    }

    Mat4 transpose(const Mat4& m)
    {
        Mat4 t;
        for (int row = 0; row < 4; ++row)
            for (int col = 0; col < 4; ++col)
                t(row, col) = m(col, row);
        return t;
    }

    namespace
    {
        // Cofactor expansion on a flat column-major array (the classic MESA gluInvertMatrix layout).
        // Fills `inv` with the adjugate and returns the determinant.
        float adjugate(const float* a, float* inv)
        {
            inv[0] = a[5] * a[10] * a[15] - a[5] * a[11] * a[14] - a[9] * a[6] * a[15] + a[9] * a[7] * a[14] + a[13] * a[6] * a[11] - a[13] * a[7] * a[10];
            inv[4] = -a[4] * a[10] * a[15] + a[4] * a[11] * a[14] + a[8] * a[6] * a[15] - a[8] * a[7] * a[14] - a[12] * a[6] * a[11] + a[12] * a[7] * a[10];
            inv[8] = a[4] * a[9] * a[15] - a[4] * a[11] * a[13] - a[8] * a[5] * a[15] + a[8] * a[7] * a[13] + a[12] * a[5] * a[11] - a[12] * a[7] * a[9];
            inv[12] = -a[4] * a[9] * a[14] + a[4] * a[10] * a[13] + a[8] * a[5] * a[14] - a[8] * a[6] * a[13] - a[12] * a[5] * a[10] + a[12] * a[6] * a[9];
            inv[1] = -a[1] * a[10] * a[15] + a[1] * a[11] * a[14] + a[9] * a[2] * a[15] - a[9] * a[3] * a[14] - a[13] * a[2] * a[11] + a[13] * a[3] * a[10];
            inv[5] = a[0] * a[10] * a[15] - a[0] * a[11] * a[14] - a[8] * a[2] * a[15] + a[8] * a[3] * a[14] + a[12] * a[2] * a[11] - a[12] * a[3] * a[10];
            inv[9] = -a[0] * a[9] * a[15] + a[0] * a[11] * a[13] + a[8] * a[1] * a[15] - a[8] * a[3] * a[13] - a[12] * a[1] * a[11] + a[12] * a[3] * a[9];
            inv[13] = a[0] * a[9] * a[14] - a[0] * a[10] * a[13] - a[8] * a[1] * a[14] + a[8] * a[2] * a[13] + a[12] * a[1] * a[10] - a[12] * a[2] * a[9];
            inv[2] = a[1] * a[6] * a[15] - a[1] * a[7] * a[14] - a[5] * a[2] * a[15] + a[5] * a[3] * a[14] + a[13] * a[2] * a[7] - a[13] * a[3] * a[6];
            inv[6] = -a[0] * a[6] * a[15] + a[0] * a[7] * a[14] + a[4] * a[2] * a[15] - a[4] * a[3] * a[14] - a[12] * a[2] * a[7] + a[12] * a[3] * a[6];
            inv[10] = a[0] * a[5] * a[15] - a[0] * a[7] * a[13] - a[4] * a[1] * a[15] + a[4] * a[3] * a[13] + a[12] * a[1] * a[7] - a[12] * a[3] * a[5];
            inv[14] = -a[0] * a[5] * a[14] + a[0] * a[6] * a[13] + a[4] * a[1] * a[14] - a[4] * a[2] * a[13] - a[12] * a[1] * a[6] + a[12] * a[2] * a[5];
            inv[3] = -a[1] * a[6] * a[11] + a[1] * a[7] * a[10] + a[5] * a[2] * a[11] - a[5] * a[3] * a[10] - a[9] * a[2] * a[7] + a[9] * a[3] * a[6];
            inv[7] = a[0] * a[6] * a[11] - a[0] * a[7] * a[10] - a[4] * a[2] * a[11] + a[4] * a[3] * a[10] + a[8] * a[2] * a[7] - a[8] * a[3] * a[6];
            inv[11] = -a[0] * a[5] * a[11] + a[0] * a[7] * a[9] + a[4] * a[1] * a[11] - a[4] * a[3] * a[9] - a[8] * a[1] * a[7] + a[8] * a[3] * a[5];
            inv[15] = a[0] * a[5] * a[10] - a[0] * a[6] * a[9] - a[4] * a[1] * a[10] + a[4] * a[2] * a[9] + a[8] * a[1] * a[6] - a[8] * a[2] * a[5];

            return a[0] * inv[0] + a[1] * inv[4] + a[2] * inv[8] + a[3] * inv[12];
        }
    }

    float determinant(const Mat4& m)
    {
        float adj[16];
        return adjugate(m.data(), adj);
    }

    Mat4 inverse(const Mat4& m)
    {
        float adj[16];
        const float det = adjugate(m.data(), adj);
        if (std::abs(det) < 1e-12f)
        {
            ARC_ASSERT(false, "inverse: matrix is singular");
            return Mat4::identity();
        }

        const float invDet = 1.0f / det;
        Mat4 result;
        for (int col = 0; col < 4; ++col)
            for (int row = 0; row < 4; ++row)
                result(row, col) = adj[col * 4 + row] * invDet;
        return result;
    }

    Mat4 normalMatrix(const Mat4& model)
    {
        Mat4 linear = model;
        linear.column(3) = {0.0f, 0.0f, 0.0f, 1.0f};
        return transpose(inverse(linear));
    }

    bool nearlyEqual(const Mat4& a, const Mat4& b, float epsilon)
    {
        for (int col = 0; col < 4; ++col)
            if (!nearlyEqual(a.column(col), b.column(col), epsilon))
                return false;
        return true;
    }
}

#pragma once

#include "Math/Math.h"

namespace arc
{
    class GameObject;

    /// A GameObject's position, rotation and scale relative to its parent (or the world, for an
    /// object without one), and the world matrix that results.
    ///
    /// The world matrix is computed when first asked for and cached. Changing a local value, or the
    /// parent, marks this transform and every one below it dirty; a dirty transform's descendants
    /// are always dirty too, so marking stops at the first one that already is.
    class Transform
    {
    public:
        explicit Transform(GameObject& owner) : m_owner(owner) {}

        Transform(const Transform&) = delete;
        Transform& operator=(const Transform&) = delete;

        [[nodiscard]] const Vec3& localPosition() const { return m_position; }
        [[nodiscard]] const Quat& localRotation() const { return m_rotation; }
        [[nodiscard]] const Vec3& localScale() const { return m_scale; }

        void setLocalPosition(const Vec3& position);
        /// Normalised on the way in.
        void setLocalRotation(const Quat& rotation);
        void setLocalScale(const Vec3& scale);
        void setLocal(const Vec3& position, const Quat& rotation, const Vec3& scale);

        /// Local to parent space: translation * rotation * scale.
        [[nodiscard]] Mat4 localMatrix() const { return Mat4::trs(m_position, m_rotation, m_scale); }

        /// Local to world space.
        [[nodiscard]] const Mat4& worldMatrix() const;
        [[nodiscard]] Vec3 worldPosition() const { return worldMatrix().translationPart(); }

        /// Sets the local values so the world matrix becomes `world`, as far as TRS can express it
        /// under the current parent (see decompose()). Returns false, and changes nothing, if the
        /// result has a zero scale axis or the parent's matrix cannot be inverted.
        bool setWorldMatrix(const Mat4& world);

        /// Copies the local values; the owner and parent are not affected.
        void copyLocalFrom(const Transform& other);

    private:
        friend class GameObject;
        void markWorldDirty();

        GameObject& m_owner;
        Vec3 m_position;
        Quat m_rotation;
        Vec3 m_scale = Vec3::one();
        mutable Mat4 m_world;
        mutable bool m_worldDirty = true;
    };
}

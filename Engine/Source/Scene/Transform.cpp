#include "Scene/Transform.h"

#include "Scene/GameObject.h"

namespace arc
{
    void Transform::setLocalPosition(const Vec3& position)
    {
        m_position = position;
        markWorldDirty();
    }

    void Transform::setLocalRotation(const Quat& rotation)
    {
        m_rotation = normalize(rotation);
        markWorldDirty();
    }

    void Transform::setLocalScale(const Vec3& scale)
    {
        m_scale = scale;
        markWorldDirty();
    }

    void Transform::setLocal(const Vec3& position, const Quat& rotation, const Vec3& scale)
    {
        m_position = position;
        m_rotation = normalize(rotation);
        m_scale = scale;
        markWorldDirty();
    }

    void Transform::copyLocalFrom(const Transform& other)
    {
        setLocal(other.m_position, other.m_rotation, other.m_scale);
    }

    const Mat4& Transform::worldMatrix() const
    {
        if (m_worldDirty)
        {
            const GameObject* parent = m_owner.parent();
            m_world = parent ? parent->transform().worldMatrix() * localMatrix() : localMatrix();
            m_worldDirty = false;
        }
        return m_world;
    }

    bool Transform::setWorldMatrix(const Mat4& world)
    {
        Mat4 local = world;
        if (const GameObject* parent = m_owner.parent())
        {
            const Mat4& parentWorld = parent->transform().worldMatrix();
            if (std::abs(determinant(parentWorld)) <= kEpsilon * kEpsilon)
                return false;
            local = inverse(parentWorld) * world;
        }

        const std::optional<Decomposed> trs = decompose(local);
        if (!trs)
            return false;
        setLocal(trs->translation, trs->rotation, trs->scale);
        return true;
    }

    void Transform::markWorldDirty()
    {
        if (m_worldDirty)
            return;
        m_worldDirty = true;
        for (GameObject* child : m_owner.children())
            child->transform().markWorldDirty();
    }
}

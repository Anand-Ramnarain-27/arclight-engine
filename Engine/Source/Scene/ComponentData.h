#pragma once

#include "Math/Math.h"
#include "Scene/Uuid.h"

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>

namespace arc
{
    class ResourceManager;

    /// The saved fields of one component: named values, stored as a JSON object in scene and prefab
    /// files. Components write and read it in Component::save() and Component::load(), so the JSON
    /// library stays out of every header.
    ///
    /// Getters return nothing when a key is missing or holds the wrong kind of value, so a
    /// component can keep its default for a field an older file does not have.
    class ComponentData
    {
    public:
        ComponentData();
        ~ComponentData();
        ComponentData(const ComponentData& other);
        ComponentData& operator=(const ComponentData& other);
        ComponentData(ComponentData&&) noexcept;
        ComponentData& operator=(ComponentData&&) noexcept;

        void setBool(std::string_view key, bool value);
        void setInt(std::string_view key, int64_t value);
        /// Written with the fewest digits that read back as the same float, so files stay readable.
        void setFloat(std::string_view key, float value);
        void setString(std::string_view key, std::string_view value);
        void setVec3(std::string_view key, const Vec3& value);
        void setVec4(std::string_view key, const Vec4& value);
        void setQuat(std::string_view key, const Quat& value);
        void setUuid(std::string_view key, Uuid value);

        [[nodiscard]] std::optional<bool> getBool(std::string_view key) const;
        [[nodiscard]] std::optional<int64_t> getInt(std::string_view key) const;
        [[nodiscard]] std::optional<float> getFloat(std::string_view key) const;
        [[nodiscard]] std::optional<std::string> getString(std::string_view key) const;
        [[nodiscard]] std::optional<Vec3> getVec3(std::string_view key) const;
        [[nodiscard]] std::optional<Vec4> getVec4(std::string_view key) const;
        [[nodiscard]] std::optional<Quat> getQuat(std::string_view key) const;
        [[nodiscard]] std::optional<Uuid> getUuid(std::string_view key) const;

        [[nodiscard]] bool empty() const;

        /// The JSON-backed storage, for the scene serializer.
        struct Storage;
        [[nodiscard]] Storage& storage() { return *m_storage; }
        [[nodiscard]] const Storage& storage() const { return *m_storage; }

    private:
        std::unique_ptr<Storage> m_storage;
    };

    /// What components need from outside while they are loaded.
    struct ComponentLoadContext
    {
        /// Resolves asset ids; null when assets are not to be loaded.
        ResourceManager* resources = nullptr;
    };
}

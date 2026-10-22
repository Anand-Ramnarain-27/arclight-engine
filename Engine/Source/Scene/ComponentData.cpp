#include "Scene/ComponentDataStorage.h"

#include <array>
#include <charconv>
#include <cmath>

namespace arc
{
    namespace
    {
        using Json = nlohmann::ordered_json;

        std::optional<float> toFloat(const Json& value)
        {
            if (!value.is_number())
                return std::nullopt;
            const double number = value.get<double>();
            if (!std::isfinite(number))
                return std::nullopt;
            return static_cast<float>(number);
        }

        template <size_t N>
        std::optional<std::array<float, N>> toFloats(const Json* value)
        {
            if (!value || !value->is_array() || value->size() != N)
                return std::nullopt;
            std::array<float, N> result{};
            for (size_t i = 0; i < N; ++i)
            {
                const std::optional<float> element = toFloat((*value)[i]);
                if (!element)
                    return std::nullopt;
                result[i] = *element;
            }
            return result;
        }
    }

    double shortestDouble(float value)
    {
        if (!std::isfinite(value))
            return 0.0;
        std::array<char, 32> text{};
        const auto written = std::to_chars(text.data(), text.data() + text.size(), value);
        double result = 0.0;
        std::from_chars(text.data(), written.ptr, result);
        return result;
    }

    ComponentData::ComponentData() : m_storage(std::make_unique<Storage>()) {}
    ComponentData::~ComponentData() = default;
    ComponentData::ComponentData(const ComponentData& other) : m_storage(std::make_unique<Storage>(*other.m_storage)) {}
    ComponentData& ComponentData::operator=(const ComponentData& other)
    {
        if (this != &other)
            m_storage = std::make_unique<Storage>(*other.m_storage);
        return *this;
    }
    ComponentData::ComponentData(ComponentData&&) noexcept = default;
    ComponentData& ComponentData::operator=(ComponentData&&) noexcept = default;

    namespace
    {
        const Json* find(const Json& object, std::string_view key)
        {
            // This nlohmann/json's ordered_map has no lookup by string_view.
            const auto found = object.find(std::string(key));
            return found != object.end() ? &*found : nullptr;
        }
    }

    void ComponentData::setBool(std::string_view key, bool value) { m_storage->object[std::string(key)] = value; }
    void ComponentData::setInt(std::string_view key, int64_t value) { m_storage->object[std::string(key)] = value; }
    void ComponentData::setFloat(std::string_view key, float value)
    {
        m_storage->object[std::string(key)] = shortestDouble(value);
    }
    void ComponentData::setString(std::string_view key, std::string_view value)
    {
        m_storage->object[std::string(key)] = std::string(value);
    }
    void ComponentData::setVec3(std::string_view key, const Vec3& value)
    {
        m_storage->object[std::string(key)] = {shortestDouble(value.x), shortestDouble(value.y), shortestDouble(value.z)};
    }
    void ComponentData::setVec4(std::string_view key, const Vec4& value)
    {
        m_storage->object[std::string(key)] = {shortestDouble(value.x), shortestDouble(value.y), shortestDouble(value.z),
                                               shortestDouble(value.w)};
    }
    void ComponentData::setQuat(std::string_view key, const Quat& value)
    {
        m_storage->object[std::string(key)] = {shortestDouble(value.x), shortestDouble(value.y), shortestDouble(value.z),
                                               shortestDouble(value.w)};
    }
    void ComponentData::setUuid(std::string_view key, Uuid value) { m_storage->object[std::string(key)] = value.toString(); }

    std::optional<bool> ComponentData::getBool(std::string_view key) const
    {
        const Json* value = find(m_storage->object, key);
        return value && value->is_boolean() ? std::optional<bool>(value->get<bool>()) : std::nullopt;
    }

    std::optional<int64_t> ComponentData::getInt(std::string_view key) const
    {
        const Json* value = find(m_storage->object, key);
        return value && value->is_number_integer() ? std::optional<int64_t>(value->get<int64_t>()) : std::nullopt;
    }

    std::optional<float> ComponentData::getFloat(std::string_view key) const
    {
        const Json* value = find(m_storage->object, key);
        return value ? toFloat(*value) : std::nullopt;
    }

    std::optional<std::string> ComponentData::getString(std::string_view key) const
    {
        const Json* value = find(m_storage->object, key);
        return value && value->is_string() ? std::optional<std::string>(value->get<std::string>()) : std::nullopt;
    }

    std::optional<Vec3> ComponentData::getVec3(std::string_view key) const
    {
        const std::optional<std::array<float, 3>> v = toFloats<3>(find(m_storage->object, key));
        return v ? std::optional<Vec3>(Vec3{(*v)[0], (*v)[1], (*v)[2]}) : std::nullopt;
    }

    std::optional<Vec4> ComponentData::getVec4(std::string_view key) const
    {
        const std::optional<std::array<float, 4>> v = toFloats<4>(find(m_storage->object, key));
        return v ? std::optional<Vec4>(Vec4{(*v)[0], (*v)[1], (*v)[2], (*v)[3]}) : std::nullopt;
    }

    std::optional<Quat> ComponentData::getQuat(std::string_view key) const
    {
        const std::optional<std::array<float, 4>> v = toFloats<4>(find(m_storage->object, key));
        if (!v)
            return std::nullopt;
        Quat q;
        q.x = (*v)[0];
        q.y = (*v)[1];
        q.z = (*v)[2];
        q.w = (*v)[3];
        return q;
    }

    std::optional<Uuid> ComponentData::getUuid(std::string_view key) const
    {
        const std::optional<std::string> text = getString(key);
        return text ? Uuid::fromString(*text) : std::nullopt;
    }

    bool ComponentData::empty() const
    {
        return m_storage->object.empty();
    }
}

#include "Scene/SceneSerializer.h"

#include "Core/Log.h"
#include "Scene/ComponentDataStorage.h"
#include "Scene/ComponentRegistry.h"
#include "Scene/Scene.h"

#include <array>
#include <cmath>
#include <optional>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace arc
{
    namespace
    {
        using Json = nlohmann::ordered_json;

        constexpr const char* kSceneFormat = "arclight-scene";
        constexpr const char* kPrefabFormat = "arclight-prefab";

        // Keys the serializer writes itself; a component's own fields may not use them.
        constexpr const char* kTypeKey = "type";
        constexpr const char* kEnabledKey = "enabled";

        struct ParsedComponent
        {
            std::string type;
            bool enabled = true;
            ComponentData data;
        };

        struct ParsedObject
        {
            Uuid id;
            std::optional<Uuid> parent;
            std::string name;
            bool active = true;
            Vec3 position;
            Quat rotation;
            Vec3 scale = Vec3::one();
            std::vector<ParsedComponent> components;
        };

        Json vector3(const Vec3& v)
        {
            return Json::array({shortestDouble(v.x), shortestDouble(v.y), shortestDouble(v.z)});
        }

        Json writeComponent(const Component& component)
        {
            Json json = Json::object();
            ComponentData data;
            if (&component.type() == &MissingComponent::staticType())
            {
                const auto& missing = static_cast<const MissingComponent&>(component);
                json[kTypeKey] = missing.typeName();
                data = missing.data();
            }
            else
            {
                json[kTypeKey] = std::string(component.type().name);
                component.save(data);
            }
            json[kEnabledKey] = component.enabled;
            for (const auto& [key, value] : data.storage().object.items())
                if (key != kTypeKey && key != kEnabledKey)
                    json[key] = value;
            return json;
        }

        // Pre-order, so every parent is written before its children and siblings keep their order.
        void writeObject(Json& objects, const GameObject& object, const GameObject* stopParent)
        {
            Json json = Json::object();
            json["id"] = object.uuid().toString();
            json["name"] = object.name();
            if (object.parent() && object.parent() != stopParent)
                json["parent"] = object.parent()->uuid().toString();
            json["active"] = object.isActiveSelf();
            const Transform& transform = object.transform();
            json["position"] = vector3(transform.localPosition());
            const Quat& rotation = transform.localRotation();
            json["rotation"] = Json::array({shortestDouble(rotation.x), shortestDouble(rotation.y),
                                            shortestDouble(rotation.z), shortestDouble(rotation.w)});
            json["scale"] = vector3(transform.localScale());
            Json components = Json::array();
            for (const std::unique_ptr<Component>& component : object.components())
                components.push_back(writeComponent(*component));
            json["components"] = std::move(components);
            objects.push_back(std::move(json));

            for (const GameObject* child : object.children())
                writeObject(objects, *child, stopParent);
        }

        std::string document(const char* format, Json objects)
        {
            Json json = Json::object();
            json["format"] = format;
            json["version"] = kSceneFormatVersion;
            json["objects"] = std::move(objects);
            return json.dump(2) + "\n";
        }

        bool fail(std::string* error, std::string message)
        {
            if (error)
                *error = std::move(message);
            return false;
        }

        std::optional<std::array<float, 4>> floats(const Json& object, const char* key, size_t count)
        {
            const auto found = object.find(key);
            if (found == object.end() || !found->is_array() || found->size() != count)
                return std::nullopt;
            std::array<float, 4> result{};
            for (size_t i = 0; i < count; ++i)
            {
                const Json& element = (*found)[i];
                if (!element.is_number() || !std::isfinite(element.get<double>()))
                    return std::nullopt;
                result[i] = static_cast<float>(element.get<double>());
            }
            return result;
        }

        ParsedObject parseObject(const Json& json, size_t index)
        {
            ParsedObject object;
            const std::optional<Uuid> id =
                json.contains("id") && json["id"].is_string() ? Uuid::fromString(json["id"].get<std::string>()) : std::nullopt;
            if (id && !id->isNull())
                object.id = *id;
            else
                ARC_LOG_WARN("Object {} has no valid id; it gets a new one", index);

            if (json.contains("parent") && json["parent"].is_string())
                object.parent = Uuid::fromString(json["parent"].get<std::string>());
            object.name = json.contains("name") && json["name"].is_string() ? json["name"].get<std::string>() : "GameObject";
            object.active = !json.contains("active") || !json["active"].is_boolean() || json["active"].get<bool>();

            if (const auto position = floats(json, "position", 3))
                object.position = {(*position)[0], (*position)[1], (*position)[2]};
            if (const auto scale = floats(json, "scale", 3))
                object.scale = {(*scale)[0], (*scale)[1], (*scale)[2]};
            if (const auto rotation = floats(json, "rotation", 4))
            {
                const Quat q((*rotation)[0], (*rotation)[1], (*rotation)[2], (*rotation)[3]);
                const float length = std::sqrt(q.x * q.x + q.y * q.y + q.z * q.z + q.w * q.w);
                // Rounded or hand-edited values are renormalised; a zero quaternion means no rotation.
                if (length > 1e-6f)
                    object.rotation = Quat(q.x / length, q.y / length, q.z / length, q.w / length);
            }

            if (json.contains("components") && json["components"].is_array())
                for (const Json& component : json["components"])
                {
                    if (!component.is_object() || !component.contains(kTypeKey) || !component[kTypeKey].is_string())
                    {
                        ARC_LOG_WARN("'{}' has a component without a type; it is skipped", object.name);
                        continue;
                    }
                    ParsedComponent parsed;
                    parsed.type = component[kTypeKey].get<std::string>();
                    parsed.enabled = !component.contains(kEnabledKey) || !component[kEnabledKey].is_boolean() ||
                                     component[kEnabledKey].get<bool>();
                    for (const auto& [key, value] : component.items())
                        if (key != kTypeKey && key != kEnabledKey)
                            parsed.data.storage().object[key] = value;
                    object.components.push_back(std::move(parsed));
                }
            return object;
        }

        bool parse(std::string_view text, const char* format, std::vector<ParsedObject>& objects, std::string* error)
        {
            const Json json = Json::parse(text.begin(), text.end(), nullptr, false);
            if (json.is_discarded() || !json.is_object())
                return fail(error, "not a JSON object");
            if (!json.contains("format") || !json["format"].is_string() || json["format"].get<std::string>() != format)
                return fail(error, std::string("not an ") + format + " file");
            if (!json.contains("version") || !json["version"].is_number_integer())
                return fail(error, "no format version");
            if (json["version"].get<int>() > kSceneFormatVersion)
                return fail(error, "written by a newer version of Arclight (format version " +
                                       std::to_string(json["version"].get<int>()) + ")");
            if (!json.contains("objects") || !json["objects"].is_array())
                return fail(error, "no list of objects");

            const Json& list = json["objects"];
            objects.reserve(list.size());
            for (size_t i = 0; i < list.size(); ++i)
            {
                if (!list[i].is_object())
                    return fail(error, "object " + std::to_string(i) + " is not a JSON object");
                objects.push_back(parseObject(list[i], i));
            }
            return true;
        }

        void addComponents(GameObject& object, std::vector<ParsedComponent>& components, const SceneLoadOptions& options)
        {
            const ComponentRegistry& registry = options.components ? *options.components : ComponentRegistry::engine();
            for (ParsedComponent& parsed : components)
            {
                std::unique_ptr<Component> component = registry.create(parsed.type);
                if (component)
                {
                    component->load(parsed.data, options.context);
                }
                else
                {
                    ARC_LOG_WARN("'{}' has a component of unknown type '{}'; it is kept as it is", object.name(), parsed.type);
                    component = std::make_unique<MissingComponent>(parsed.type, std::move(parsed.data));
                }
                component->enabled = parsed.enabled;
                object.addComponent(std::move(component));
            }
        }

        GameObject& build(Scene& scene, ParsedObject& parsed, GameObject* parent, Uuid id, const SceneLoadOptions& options)
        {
            GameObject& object = scene.create(std::move(parsed.name), parent, id);
            object.setActive(parsed.active);
            object.transform().setLocal(parsed.position, parsed.rotation, parsed.scale);
            addComponents(object, parsed.components, options);
            return object;
        }
    }

    std::string writeScene(const Scene& scene)
    {
        Json objects = Json::array();
        for (const GameObject* root : scene.roots())
            writeObject(objects, *root, nullptr);
        return document(kSceneFormat, std::move(objects));
    }

    std::string writePrefab(const GameObject& root)
    {
        Json objects = Json::array();
        writeObject(objects, root, root.parent());
        return document(kPrefabFormat, std::move(objects));
    }

    bool readScene(Scene& scene, std::string_view json, const SceneLoadOptions& options, std::string* error)
    {
        std::vector<ParsedObject> objects;
        if (!parse(json, kSceneFormat, objects, error))
            return false;

        scene.clear();
        std::unordered_map<Uuid, GameObject*> created;
        for (ParsedObject& parsed : objects)
        {
            Uuid id = parsed.id;
            if (id.isNull() || created.contains(id))
            {
                if (!id.isNull())
                    ARC_LOG_WARN("'{}' repeats the id {}; it gets a new one", parsed.name, id.toString());
                id = Uuid::generate();
            }
            GameObject* parent = nullptr;
            if (parsed.parent)
            {
                const auto found = created.find(*parsed.parent);
                if (found != created.end())
                    parent = found->second;
                else
                    ARC_LOG_WARN("The parent of '{}' is not before it in the file; it is placed at the top level", parsed.name);
            }
            created.emplace(id, &build(scene, parsed, parent, id, options));
        }
        return true;
    }

    GameObject* instantiatePrefab(Scene& scene, std::string_view json, GameObject* parent, const SceneLoadOptions& options,
                                  std::string* error)
    {
        std::vector<ParsedObject> objects;
        if (!parse(json, kPrefabFormat, objects, error))
            return nullptr;
        if (objects.empty())
        {
            fail(error, "the prefab has no objects");
            return nullptr;
        }

        // Ids in the file only link parents and children within it; every copy gets fresh ones.
        std::unordered_map<Uuid, GameObject*> created;
        GameObject* root = nullptr;
        for (ParsedObject& parsed : objects)
        {
            GameObject* objectParent = parent;
            if (root)
            {
                const auto found = parsed.parent ? created.find(*parsed.parent) : created.end();
                objectParent = found != created.end() ? found->second : root;
            }
            const Uuid fileId = parsed.id;
            GameObject& object = build(scene, parsed, objectParent, Uuid::generate(), options);
            if (!root)
                root = &object;
            if (!fileId.isNull())
                created.try_emplace(fileId, &object);
        }
        return root;
    }
}

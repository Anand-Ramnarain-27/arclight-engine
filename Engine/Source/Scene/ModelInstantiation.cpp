#include "Scene/ModelInstantiation.h"

#include "Renderer/Model.h"
#include "Scene/MeshComponent.h"
#include "Scene/Scene.h"

#include <format>
#include <vector>

namespace arc
{
    GameObject& instantiateModel(Scene& scene, const std::shared_ptr<const Model>& model, GameObject* parent)
    {
        GameObject& root = scene.create(model->name(), parent);

        // The importer lists parents before children, so every node's parent object already exists.
        const std::vector<NodeData>& nodes = model->nodes();
        std::vector<GameObject*> objects(nodes.size(), nullptr);
        for (size_t i = 0; i < nodes.size(); ++i)
        {
            const NodeData& node = nodes[i];
            GameObject* nodeParent = node.parent >= 0 ? objects[static_cast<size_t>(node.parent)] : &root;
            GameObject& object = scene.create(node.name.empty() ? std::format("Node {}", i) : node.name, nodeParent);
            object.transform().setLocal(node.translation, node.rotation, node.scale);
            if (node.mesh >= 0)
                object.addComponent<MeshComponent>(model, static_cast<uint32_t>(node.mesh));
            objects[i] = &object;
        }
        return root;
    }
}

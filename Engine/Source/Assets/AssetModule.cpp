#include "Assets/AssetModule.h"

#include "Application/Application.h"

namespace arc
{
    AssetModule::AssetModule(Application& app) : Module(app) {}

    AssetModule::~AssetModule() = default;

    bool AssetModule::init()
    {
        Renderer& renderer = app().renderer();
        m_database = std::make_unique<AssetDatabase>(app().fileSystem());
        m_database->refresh();
        m_resources = std::make_unique<ResourceManager>(*m_database, renderer.device(), renderer.geometry());
        return true;
    }

    void AssetModule::shutdown()
    {
        m_resources.reset();
        m_database.reset();
    }
}

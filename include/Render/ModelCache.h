#pragma once
#include "Render/Assets/AssetManager.h"
#include "Render/API/IModel.h"
#include <memory>
#include <string>

// Back-compat shim. The model cache + loader now live in AssetManager (which also drives the async,
// threaded load path). These thin forwards keep the original synchronous call sites working unchanged:
//   - MeshComponent::deserialize  -> ModelCache::get   (blocking, shared per (path,gamma,flip))
//   - SceneManager hot-reload     -> ModelCache::clear (drop cached GL while the context is alive)
//   - Engine::shutdown            -> AssetManager::shutdown (stops workers too; see Engine.h)
namespace ModelCache {

    inline std::shared_ptr<IModel> get(const std::string& path, bool gamma = false, bool flipUVs = false) {
        return AssetManager::instance().getSync(path, gamma, flipUVs);
    }

    inline void clear() { AssetManager::instance().clearCache(); }

}

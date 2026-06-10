#pragma once
#include "Core/Components/script.h"
#include "Core/Components/GameObject.h"
#include "Core/Components/TransformComponent3D.h"
#include "Core/Engine/Services/ComponentRegistry.h"
#include "Core/Engine/Services/GameObjectLoader.h"   // instantiate the prefab JSON at runtime
#include "Core/Engine/Services/LoadContext.h"
#include "Core/Engine/SceneManager.h"                // where the spawned object is parented
#include "Components/Input/InputComponent.h"
#include "Components/Rendering/CameraComponent.h"
#include "Components/Physics/RigidBodyComponent3D.h" // optional launch velocity
#include <glm/glm.hpp>
#include <nlohmann/json.hpp>
#include <vector>
#include <string>

//TODO: Add get owner scene, and make the owner scene load the object and manage its destruction and creation

// Spawns a prefab (a GameObject defined as a JSON file) at the camera's position
// each time the spawn action fires. The cube is NOT hard-coded — it is loaded from
// `cubePrefab` through the same GameObjectLoader the scene files use, so any prefab
// (box, sphere, a whole composite object) can be dropped in by changing the path.
//
// Attach this to the camera object (the loose MainCamera). That matters: the
// SceneManager updates scenes first, then loose objects, so when this script adds an
// object to a scene the scene's own update loop has already finished for the frame —
// no iterator invalidation. If you attach it to an in-scene object instead, defer the
// insertion, or you'll mutate the scene's object map mid-iteration.
//
// JSON: { "type": "CubeSpawner",
//         "prefab": "assets/gameObjects/rigid_box.json",
//         "scene": "Level1",          // active scene to parent spawns into
//         "action": "SpawnCube",      // input action that triggers a spawn
//         "spawnDistance": 2.0,       // meters in front of the camera
//         "launchSpeed": 0.0 }        // forward velocity given to the rigid body (0 = drop)
class CubeSpawner : public Script {
    InputComponent* in = nullptr;
    InputManager* im = nullptr;
    TransformComponent3D* camTf = nullptr; // camera transform: position + facing
    int spawnCount = 0;                    // makes each spawned object's name unique

public:
    std::vector<GameObject*> spawnedCubes; // borrowed; the scene owns the objects

    std::string cubePrefab = "assets/gameObjects/rigid_box.json";
    std::string targetScene = "physics_level";    // scene name from the manifest, use owner
    std::string spawnAction = "SpawnCube";
    float spawnDistance = 4.0f;            // offset along camera forward so it isn't inside the lens
    float launchSpeed   = 0.0f;            // >0 throws the cube along the camera forward

    void deserialize(const nlohmann::json& j, LoadContext&) override {
        // "prefab" is the canonical key; "cubeModelPath" kept as a legacy alias.
        cubePrefab    = j.value("prefab",       j.value("cubeModelPath", cubePrefab));
        targetScene   = j.value("scene",        targetScene);
        spawnAction   = j.value("action",       spawnAction);
        spawnDistance = j.value("spawnDistance", spawnDistance);
        launchSpeed   = j.value("launchSpeed",  launchSpeed);
    }

    void start() override {
        in = owner->getComponent<InputComponent>();
        if (!in) throw MissingComponentException("CubeSpawner", "InputComponent");
        im = in->GetManager();

        camTf = owner->getComponent<TransformComponent3D>();
        if (!camTf) throw MissingComponentException("CubeSpawner", "TransformComponent3D");

        // Fire once per key press (on the release edge), not every frame held.
        in->BindReleased(spawnAction, [this](float) { if (Enabled) spawn(); });
    }

private:
    void spawn() {
        Scene* scene = SceneManager::instance().getScene(targetScene);
        if (!scene) {
            std::cerr << "[CubeSpawner] target scene \"" << targetScene
                      << "\" not found — cannot spawn\n";
            return;
        }

        // Build the prefab. Inject the live InputManager so prefabs that carry their
        // own Input component still wire up (the rigid box doesn't, but it's free).
        LoadContext ctx;
        ctx.input = im;
        std::unique_ptr<GameObject> obj = GameObjectLoader::fromFile(cubePrefab, ctx);
        if (!obj) {
            std::cerr << "[CubeSpawner] failed to load prefab " << cubePrefab << "\n";
            return;
        }

        // Unique name: Scene::addObject keys by name and REPLACES on a clash, so every
        // spawn must be distinct or each new cube would evict the previous one.
        obj->Name += "_" + std::to_string(spawnCount++);

        // Place it at the camera, nudged forward so it spawns in view rather than
        // clipping the near plane. Set BEFORE start(): the rigid body/collider snapshot
        // the owner transform in start() via useOwnerPosition().
        const glm::vec3 spawnPos = camTf->Position + camTf->forward() * spawnDistance;
        if (auto* tf = obj->getComponent<TransformComponent3D>())
            tf->Position = spawnPos;

        // Hand ownership to the scene, then start it (the scene already started, so
        // addObject does not start for us). start() registers the rigid body with the
        // physics world.
        GameObject* raw = scene->addObject(std::move(obj));
        raw->start();

        // Optional throw: give the body a forward velocity through the physics API
        // (wakes it and respects the sleep system).
        if (launchSpeed > 0.0f) {
            if (auto* rb = raw->getComponent<RigidBodyComponent3D>())
                rb->SetVelocity(camTf->forward() * launchSpeed);
        }

        spawnedCubes.push_back(raw);
    }
};

REGISTER_COMPONENT(CubeSpawner, "CubeSpawner")

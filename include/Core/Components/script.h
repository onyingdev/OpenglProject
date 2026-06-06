#pragma once
#include "Core/Components/Component.h"

// A Script is a Component that holds *game-specific* behavior.
//
// Engine-provided pieces (TransformComponent, CameraComponent, InputComponent,
// MeshRenderer, RigidBody) inherit Component directly. User-written behaviors
// (CameraController, PlayerController, EnemyAI, JumpAbility...) inherit Script.

//   - Enabled flag: pause a behavior without removing it from the GameObject.
//   - tick(dt):     override this instead of update(). Skipped when Enabled is false.

// Note on callback-style scripts: if a script registers callbacks elsewhere
// (e.g. InputComponent::BindPressed), those callbacks aren't owned by the
// script and won't be suppressed by Enabled. Either check Enabled inside the
// callback, or drive the behavior from tick() instead.
class Script : public Component {
public:
    bool Enabled = true;

    virtual void tick(float dt) {} //update every frame

    virtual void fixedTick(float dt) {} // update per step

    // Component::update is sealed for Script. Use tick() instead.
    void update(float dt) override final {
        if (Enabled) tick(dt);
    }
};

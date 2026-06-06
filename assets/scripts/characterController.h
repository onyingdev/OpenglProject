#pragma once
#include "Core/script.h"
#include "Core/GameObject.h"
#include "Components/TransformComponent3D.h"
#include "Components/InputComponent.h"
#include <glm/glm.hpp>
#include "Components/ColliderComponent3D.h"

// Character controller. Movement is integrated in tick() so we can model
// acceleration and deceleration properly (input handlers only fire while held,
// so they can't decelerate after release).
class CharacterController : public Script {
    TransformComponent* tf = nullptr;
    InputComponent* in = nullptr;
    InputManager* im = nullptr;
    glm::vec3 Velocity{0.0f};
    MeshComponent* mesh = nullptr; // optional, for visual facing direction
    ColliderComponent3D* collider = nullptr; // optional, for collision detection

public:
    float MoveSpeed    = 3.0f;  // units / second (max speed)
    float Acceleration = 15.0f; // units / second^2 while input is held
    float Deceleration = 10.0f; // units / second^2 while no input
    glm::vec3 dir{0.0f};        //facing direction
    float RotationSpeed = 180.0f; 

    void start() override {
        tf = owner->getComponent<TransformComponent>();
        in = owner->getComponent<InputComponent>();
        mesh = owner->getComponent<MeshComponent>();
        collider = owner->getComponent<ColliderComponent3D>();
        if (!tf) throw MissingComponentException("CharacterController", "TransformComponent");
        if (!in) throw MissingComponentException("CharacterController", "InputComponent");
        if (!mesh) ComponentWarning::missing("CharacterController", "MeshComponent");
        else mesh->useOwnerPosition(); //mesh position inherits from player object
        if (!collider) ComponentWarning::missing("CharacterController", "ColliderComponent3D");
        else collider->useOwnerPosition(); collider->useOwnerRotation();//collider position inherits from player object
        im = in->GetManager();
    }

    //to apply rotation we rotate the mesh not the object itself
    //if we need a more complex implementation we can define a facing vector which can be decoupled from the 
    //transform component

    void tick(float dt) override {
        if (!Enabled) return;
        //example of how to override the update function using tick
        // Build a world-space input direction from currently-held movement keys.
        dir = glm::vec3{0.0f};
        if (im->IsActionPressed("MoveForward"))  dir += tf->forward();
        if (im->IsActionPressed("MoveBackward")) dir -= tf->forward();
        if (im->IsActionPressed("MoveRight"))    dir += tf->right();
        if (im->IsActionPressed("MoveLeft"))     dir -= tf->right();
        if (im->IsActionPressed("MoveUp"))       dir += tf->up();
        if (im->IsActionPressed("MoveDown"))     dir -= tf->up();
        if (im->IsActionPressed("RotateRight"))  mesh->localRotation().x -= RotationSpeed * dt;
        if (im->IsActionPressed("RotateLeft"))   mesh->localRotation().x += RotationSpeed * dt;

        // Normalize so diagonals aren't faster than straight lines.
        glm::vec3 target{0.0f};
        float rate = Deceleration;
        if (glm::length(dir) > 0.0001f) {
            target = glm::normalize(dir) * MoveSpeed;
            rate = Acceleration;
        }

        // Step Velocity toward target by at most rate*dt this frame.
        glm::vec3 diff = target - Velocity;
        float maxDelta = rate * dt;
        if (glm::length(diff) > maxDelta) {
            Velocity += glm::normalize(diff) * maxDelta;
        } else {
            Velocity = target;
        }

        tf->Position += Velocity * dt;
    }
};

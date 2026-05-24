#pragma once
#include "../Core/Component.h"
#include "TransformComponent3D.h"
#include "MeshComponent.h"
#include "InputComponent.h"

// Character 3D. Basic character for player model
class CharacterComponent : public Component {
    TransformComponent* tf = nullptr;
    MeshComponent* mesh = nullptr;
    InputComponent* in = nullptr; //we may or may not need this but generally character have input
    //collider
    
public:

    void start() override {
        tf = owner->getComponent<TransformComponent>();
        if (!tf) throw MissingComponentException("CharacterComponent", "TransformComponent");
        mesh = owner->getComponent<MeshComponent>();
        if (!mesh) ComponentWarning::missing("CharacterComponent", "MeshComponent");
        in = owner->getComponent<InputComponent>();
        if (!in) ComponentWarning::missing("CharacterComponent", "InputComponent");
    }
};

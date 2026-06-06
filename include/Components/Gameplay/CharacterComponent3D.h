#pragma once
#include "Core/Components/SpatialComponent3D.h"
#include "Components/Rendering/MeshComponent.h"
#include "Components/Input/InputComponent.h"

// Character 3D. Basic character for player model
class CharacterComponent : public SpatialComponent3D {
    MeshComponent* mesh = nullptr;
    InputComponent* in = nullptr; //we may or may not need this but generally character have input
    //collider
    
public:

    void start() override {
        SpatialComponent3D::start();
        mesh = owner->getComponent<MeshComponent>();
        if (!mesh) ComponentWarning::missing("CharacterComponent", "MeshComponent");
        in = owner->getComponent<InputComponent>();
        if (!in) ComponentWarning::missing("CharacterComponent", "InputComponent");
    }
};

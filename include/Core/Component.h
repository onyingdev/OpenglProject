#pragma once
#include "exceptions.h"

class GameObject;

class Component {
public:
    virtual ~Component() = default;
    virtual void start()   {}              // called once after all components attached
    virtual void update(float dt) {}       // called every frame
    GameObject* owner = nullptr;
};
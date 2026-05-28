#pragma once
#include "../Core/Component.h"
#include "../Core/GameObject.h"
#include "../../vendor/glm/glm/glm.hpp"
#include "../Components/TransformComponent3D.h"

// Spatial: a component with a local transform that can resolve each axis from
// either the owner's TransformComponent or its own local copy. Per-axis source
// is stored as a pointer so reads at draw time are a single dereference, with
// no branching. Writers (setLocal* / local*()) flip the source to local in
// the same call, so scripts never have to manage the flag manually.
class SpatialComponent : public Component {
public:
    TransformComponent localTransform = TransformComponent();

    glm::vec3 Position{0.0f};
    glm::vec3 RotationEuler{0.0f};   // yaw, pitch, roll (degrees)
    glm::vec3 Scale{1.0f};

    glm::vec3 forward() const {
        const glm::vec3& r = rotation();   // resolved per-axis, respects setLocal*/useOwner*
        float y = glm::radians(r.x), p = glm::radians(r.y);
        return glm::normalize(glm::vec3(cos(y)*cos(p), sin(p), sin(y)*cos(p)));
    }

    glm::vec3 right()   const { return glm::normalize(glm::cross(forward(), {0,1,0})); }
    glm::vec3 up()      const { return glm::normalize(glm::cross(right(), forward())); }

    void start() override {
        auto* tf = owner->getComponent<TransformComponent>();
        if (!tf) throw MissingComponentException("SpatialComponent", "TransformComponent");
        localTransform = *tf; // snapshot owner at spawn
        positionRef = &localTransform.Position;
        rotationRef = &localTransform.RotationEuler;
        scaleRef    = &localTransform.Scale;
    }

    // Read accessors — what draw code uses. Single deref, no branching.
    const glm::vec3& position() const { return *positionRef; }
    const glm::vec3& rotation() const { return *rotationRef; }
    const glm::vec3& scale()    const { return *scaleRef; }

    // Mutable accessors for in-place edits (e.g. `localRotation().x += dt*speed`).
    // Each call ensures the axis is sourced from local.
    glm::vec3& localPosition() { positionRef = &localTransform.Position;      return localTransform.Position; }
    glm::vec3& localRotation() { rotationRef = &localTransform.RotationEuler; return localTransform.RotationEuler; }
    glm::vec3& localScale()    { scaleRef    = &localTransform.Scale;         return localTransform.Scale; }

    // Setters: write the local value AND switch the axis source to local.
    void setLocalPosition(const glm::vec3& v) { localTransform.Position      = v; positionRef = &localTransform.Position; }
    void setLocalRotation(const glm::vec3& v) { localTransform.RotationEuler = v; rotationRef = &localTransform.RotationEuler; }
    void setLocalScale   (const glm::vec3& v) { localTransform.Scale         = v; scaleRef    = &localTransform.Scale; }

    // Opt in to following the owner for a given axis.
    void useOwnerPosition() { positionRef = &ownerTransform()->Position; }
    void useOwnerRotation() { rotationRef = &ownerTransform()->RotationEuler; }
    void useOwnerScale()    { scaleRef    = &ownerTransform()->Scale; }

private:
    const glm::vec3* positionRef = nullptr;
    const glm::vec3* rotationRef = nullptr;
    const glm::vec3* scaleRef    = nullptr;

    TransformComponent* ownerTransform() {
        auto* tf = owner->getComponent<TransformComponent>();
        if (!tf) throw MissingComponentException("SpatialComponent", "TransformComponent");
        return tf;
    }
};

//TODO: optimise spatial component to save memory, since it uses TransformComponent3D it
//has some redundant data, we can make a spatial component that only has the local transform and the source.

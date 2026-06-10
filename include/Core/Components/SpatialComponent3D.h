#pragma once
#include "Core/Components/Component.h"
#include "Core/Components/GameObject.h"
#include <glm/glm.hpp>
#include "Core/Components/TransformComponent3D.h"

// Spatial: a component with a local transform that can resolve each axis from
// either the owner's TransformComponent or its own local copy. Per-axis source
// is stored as a pointer so reads at draw time are a single dereference, with
// no branching. Writers (setLocal* / local*()) flip the source to local in
// the same call, so scripts never have to manage the flag manually.
class SpatialComponent3D : public Component {
public:
    // This component's own transform. Position/rotation/scale live here; the
    // ref pointers below select, per axis, whether reads come from this local
    // copy or from the owner's TransformComponent (see useOwner* / setLocal*).
    // Plain Transform (not TransformComponent): no vtable/owner overhead, since
    // this is data we never register or tick as a component.
    Transform localTransform;

    glm::vec3 forward() const {
        const glm::vec3& r = rotation(); 
        float y = glm::radians(r.x), p = glm::radians(r.y);
        return glm::normalize(glm::vec3(cos(y)*cos(p), sin(p), sin(y)*cos(p)));
    }

    glm::vec3 right()   const { return glm::normalize(glm::cross(forward(), {0,1,0})); }
    glm::vec3 up()      const { return glm::normalize(glm::cross(right(), forward())); }

    void start() override {
        auto* tf = owner->getComponent<TransformComponent3D>();
        if (!tf) throw MissingComponentException("SpatialComponent3D", "TransformComponent3D");
        localTransform = static_cast<const Transform&>(*tf); // snapshot owner's transform data at spawn
        positionRef = &localTransform.Position;
        rotationRef = &localTransform.RotationEuler;
        scaleRef    = &localTransform.Scale;
    }

    // Read accessors — what draw code uses. Single deref, no branching.
    const glm::vec3& position() const { return *positionRef; }
    const glm::vec3& rotation() const { return *rotationRef; }
    const glm::vec3& scale()    const { return *scaleRef; }

    // Quaternion rotation for rendering/colliders. By default it's DERIVED from the live Euler
    // source (so Euler-animated scripts keep working); after useOwnerOrientation() it reads the
    // owner Transform's quaternion authority directly (what a physics body integrates).
    glm::quat orientation() const {
        return orientationRef ? *orientationRef : eulerToQuat(rotation());
    }

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
    // Read the owner's quaternion authority directly (physics-driven rotation).
    void useOwnerOrientation() { orientationRef = &ownerTransform()->Orientation; }

private:
    const glm::vec3* positionRef = nullptr;
    const glm::vec3* rotationRef = nullptr;
    const glm::vec3* scaleRef    = nullptr;
    const glm::quat* orientationRef = nullptr;   // null => derive from Euler in orientation()

    TransformComponent3D* ownerTransform() {
        auto* tf = owner->getComponent<TransformComponent3D>();
        if (!tf) throw MissingComponentException("SpatialComponent3D", "TransformComponent3D");
        return tf;
    }
};

// Layout: one local TransformComponent3D + three per-axis source pointers. The
// pointers default to localTransform at start(), and useOwner*()/setLocal*()
// retarget them so reads stay a single deref with no branching.

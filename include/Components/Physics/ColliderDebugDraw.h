#pragma once
#include "Core/Components/ColliderShapes.h"
#include "Components/Physics/ColliderComponent3D.h"
#include "Components/Physics/HurtBoxComponent3D.h"
#include "Components/Physics/RigidBodyComponent3D.h"   // new merged-collider bodies
#include "Render/API/IRenderer.h"
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

// Debug visualization glue: draws collision shapes as wireframes through the renderer's
// gizmo primitives. Lives at the engine/glue layer (it's the only thing that depends on
// BOTH physics shapes and the renderer); ColliderShapes.h itself stays render-free.
// Dispatch is on ShapeKind so adding a shape only means adding a case here.

// Draw one shape at a world pose. `scale` is the owner's transform scale (shapes store
// local extents; world size = extent * scale, matching the collision math).
inline void debugDrawShape(IRenderer& r, const ColliderShape& shape,
                           const glm::vec3& pos, const glm::vec3& scale, const glm::vec3& color) {
    switch (shape.kind()) {
        case ShapeKind::Box: {
            const auto& b = static_cast<const BoxShape&>(shape);
            const glm::mat4 m = glm::translate(glm::mat4(1.0f), pos)
                              * glm::scale(glm::mat4(1.0f), 2.0f * b.HalfExtents * scale);
            r.drawWireBox(m, color);
            break;
        }
        case ShapeKind::Sphere: {
            const auto& s = static_cast<const SphereShape&>(shape);
            r.drawWireSphere(pos, s.Radius * scale.x, color);
            break;
        }
        case ShapeKind::OBB: {
            const auto& o = static_cast<const OBBShape&>(shape);
            const glm::mat4 m = glm::translate(glm::mat4(1.0f), pos)
                              * glm::mat4_cast(o.Orientation)
                              * glm::scale(glm::mat4(1.0f), 2.0f * o.HalfExtents * scale);
            r.drawWireBox(m, color);
            break;
        }
        case ShapeKind::Ball: {
            const auto& b = static_cast<const OrientedSphereShape&>(shape);
            const float rad = b.Radius * scale.x;
            r.drawWireSphere(pos, rad, color);
            r.drawLine(pos, pos + b.Orientation * glm::vec3(0, rad, 0), color);  // spin axis (shows rotation)
            break;
        }
        case ShapeKind::Plane: {
            const auto& p = static_cast<const PlaneShape&>(shape);
            glm::vec3 T, B;
            p.tangentBasis(T, B);
            // Infinite planes have no real extent — draw a large patch so it's visible.
            const glm::vec2 e = p.Infinite ? glm::vec2(10.0f) : p.HalfExtents;
            const glm::vec3 tx = T * e.x, bz = B * e.y;
            const glm::vec3 c00 = pos - tx - bz, c10 = pos + tx - bz;
            const glm::vec3 c11 = pos + tx + bz, c01 = pos - tx + bz;
            r.drawLine(c00, c10, color);
            r.drawLine(c10, c11, color);
            r.drawLine(c11, c01, color);
            r.drawLine(c01, c00, color);
            r.drawLine(pos, pos + glm::normalize(p.Normal), color);   // normal stub
            break;
        }
    }
}

// Draw every live collider (green), hurtbox (red), and new rigid-body shape (cyan). The new
// RigidBodyComponent3D owns its shape (merged collider), so it has its own registry to sweep.
// Call between beginScene/endScene.
inline void debugDrawColliders(IRenderer& r) {
    const glm::vec3 kColliderColor {0.2f, 1.0f, 0.2f};
    const glm::vec3 kHurtBoxColor  {1.0f, 0.2f, 0.2f};
    const glm::vec3 kRigidBodyColor{0.2f, 0.7f, 1.0f};

    for (auto* c : ColliderComponent3D::colliders())
        if (c && c->Shape) debugDrawShape(r, *c->Shape, c->position(), c->scale(), kColliderColor);

    for (auto* h : HurtBoxComponent3D::hurtboxes())
        if (h && h->Shape) debugDrawShape(r, *h->Shape, h->position(), h->scale(), kHurtBoxColor);

    for (auto* b : RigidBodyComponent3D::world())
        if (b && b->Shape) debugDrawShape(r, *b->Shape, b->position(), b->scale(), kRigidBodyColor);
}

#pragma once
#include <vector>
#include <algorithm>
#include <glm/glm.hpp>
#include "Core/Engine/Physics/uniform_grid.h"   // broadphase grid the manager owns

// Forward-declare only: defining add/remove/step needs nothing but the pointer type here, and
// NOT including RigidBodyComponent3D.h breaks the cycle (the component includes THIS header to
// self-register). PhysicsManager::step() is defined at the tail of RigidBodyComponent3D.h, where
// the complete type is available.
class RigidBodyComponent3D;

//physics manager owns list of bodies and uniform grid for spatial splitting.
//manages multithreading, it passes on each body the list of bodies it has to check
//for each of them assigns one thread

//PhysicsManager, manages collisions, rigid bodies and colliders (simple and complex)
//having a manager also allows scripts to access physics functions in a similar manner to how SceneManager works
//
// MIGRATION STATE — increment 1 (DONE here): the manager OWNS the body list (borrowed pointers;
// scenes own the bodies) and drives the fixed step. The solver itself still lives in
// RigidBodyComponent3D::stepWorld for now; step() just hands it the list. Next increments move the
// grid / islands / warm cache in (see the commented private layout below).
class PhysicsManager {
public:
    static PhysicsManager& instance() {
        static PhysicsManager mgr;   // Meyers singleton: the one and only root manager
        return mgr;
    }

    // --- registration (borrowed; the body's lifetime is the component's) ---
    // add(body)    ← called from RigidBodyComponent3D::start()
    // remove(body) ← called from the component's destructor
    void add(RigidBodyComponent3D* body) {
        if (body) m_bodies.push_back(body);   // push_back preserves order → deterministic solve order
    }
    void remove(RigidBodyComponent3D* body) {
        m_bodies.erase(std::remove(m_bodies.begin(), m_bodies.end(), body), m_bodies.end());
    }

    //reset physics — drop every registered body (does NOT free them; scenes own them).
    void reset() { m_bodies.clear(); }

    // --- per-frame drive ---
    // One fixed physics slice. Engine owns the accumulator and calls this N times per frame
    // (kFixedDt substeps). Defined in RigidBodyComponent3D.h (needs the complete type).
    void step(float dt);

    // --- access / queries ---
    const std::vector<RigidBodyComponent3D*>& bodies() const { return m_bodies; }
    UniformGrid&       grid()       { return m_grid; }
    const UniformGrid& grid() const { return m_grid; }

    // --- simulation range (geography gate) ---
    // Cubic by default-ish (pass equal extents), but halfExtents is a vec3 so you can clamp the
    // horizontal (x/z) and vertical (y) reach independently. Disabled until set → unbounded world.
    void setRange(const glm::vec3& center, const glm::vec3& halfExtents) {
        m_grid.rangeEnabled = true; m_grid.rangeCenter = center; m_grid.rangeHalf = halfExtents;
    }
    void setRangeCubic(const glm::vec3& center, float halfExtent) {
        setRange(center, glm::vec3(halfExtent));
    }
    void clearRange()              { m_grid.rangeEnabled = false; }
    void setRangeCenter(const glm::vec3& c) { m_grid.rangeCenter = c; }   // move the active box (e.g. follow the camera)
    void setCellSize(float s)      { if (s > 0.0f) m_grid.cellSize = s; }

    //grid → candidate PAIRS → narrowphase → CONTACTS → island build → ISLANDS → solver
    //     (pair-parallel ✓)              (union-find)        (island-parallel ✓)

    // spatial splitting
    // assign grid, this function tells the body what grids should he check passing its id.
    // then groups up the bodies that has overlapping grids
    // the grid is a list we only register grids that are active meaning when we do the intersection,
    // we set them inside the list and remove them if no body is present.

    // 1. first loop to calculate AABB (bounding box of each body)
    // 2. then calculate intersecting grid by using math since its AABB and the grid is uniform it
    // should be very fast, we dont have to check each cell.
    //example in 2D, each grid is 1*1 on corner is (1.5, 2.5) and (2.5 and 1.5) on x y
    // with the strid and some math shenannigans we should be able to pint point in which cells are the extremes
    // with that just fill the rest by interpolation.
    // issues: infinite plane, grid applies to bounded and limited worlds. Idea: simulation chunks (range, chunk and cell)

    // step 1 and 2 can be done completly parallel
    // 3. single thread loop compares overlapping grids and assign each body a list of bodies it has to check
    // 4. each group will be assign to a thread to solve the collision
    // issue: if we make a pile a huge pile will be check by just one thread. Require further investigation
    // change: contact islands, per island we should also improve the contact solver algorithm using coloring grids

    // detection done by grids, solver done by islands

private:
    // increment 1 storage: one flat list, exactly mirroring the old static bodies() registry so
    // behavior is unchanged. The split into dynamic/static/unbounded comes later.
    std::vector<RigidBodyComponent3D*> m_bodies;   // borrowed — scenes own the bodies

    // increment 2: the broadphase grid. Rebuilt each step (inside stepWorld, which has the complete
    // body type to compute AABBs); the manager owns the storage so capacity is reused across steps.
    UniformGrid m_grid;

    // --- planned layout (next increments) ---
    // std::vector<RigidBodyComponent3D*> m_dynamic;     // integrate + query grid
    // std::vector<RigidBodyComponent3D*> m_static;      // grid occupants, built once
    // std::vector<RigidBodyComponent3D*> m_unbounded;   // infinite planes, tested vs each dynamic
    //
    // // spatial hash: hash(cx,cy,cz) -> bucket. Static side persistent, dynamic side cleared/step.
    // GridHash m_staticGrid, m_dynamicGrid;
    //
    // // solve
    // std::vector<ContactConstraint> m_constraints;
    // std::unordered_map<uint64_t, CachedImpulse> m_warmCache;   // per-contact, not per-island
    // std::vector<Island> m_islands;                             // union-find result, rebuilt/step

    PhysicsManager() = default;
    PhysicsManager(const PhysicsManager&) = delete;
    PhysicsManager& operator=(const PhysicsManager&) = delete;
};


// --- solver optimization roadmap (per-island, later) ---
// Adaptive early-out — few lines, immediately makes resting scenes cheap. Do this first, today.
// Symmetric sweep + keep precomputed masses — cheap quality bumps.
// TGS soft-step — the structural win; fewer iterations, drops the position pass, better stability. The one larger investment worth making.
// Block friction / SoA / SIMD — incremental, profile-driven.
// Grid-coloring inside an oversized island — only if one island dominates after all the above.

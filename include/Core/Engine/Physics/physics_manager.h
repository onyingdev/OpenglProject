#pragma once
#include <memory>
#include <string>
#include <unordered_map>
#include <iostream>
#include <nlohmann/json.hpp>

//physics manager owns list of bodies and uniform grid for spatial splitting.
//manages multithreading, it passes on each body the list of bodies it has to check
//for each of them assigns one thread

//PhysicsManager, manages collisions, rigid bodies and colliders (simple and complex)
//having a manager also allows scripts to access physics functions in a similar manner to how SceneManager works

class PhysicsManager {
public:
    static PhysicsManager& instance() {
        static PhysicsManager mgr;   // Meyers singleton: the one and only root manager
        return mgr;
    }

    // add(body) ← called from RigidBodyComponent3D::start()
    // remove(body) ← called from the component's destructor
    // unregister(body)

    //reset physics

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

    // --- access / queries ---

    // get body

    // find body


    //find component?

    // --- per-frame drive (active scenes only) ---

    //TODO: update loose objects only, update scenes only, update specific scenes and loose object.
    //unload specific scene or object.

    void update(float dt) {

    }

    //step update, update N steps every frame

    //draw


private:
    // entry

    // list of dynamic bodies, dynamic checks agaisnt dynamic and static, inside its neighbouring grids

    // list of static bodies, static doesnt check for anything just sits there chilling.

    //grid, stride by x y z and size of cell, this can identify each cell anywhere in the world

    //solver, impulse cache for each group/island?

    // private:
    // std::vector<RigidBodyComponent3D*> m_dynamic;     // integrate + query grid
    // std::vector<RigidBodyComponent3D*> m_static;      // grid occupants, built once
    // std::vector<RigidBodyComponent3D*> m_unbounded;   // infinite planes, tested vs each dynamic

    // // spatial hash: hash(cx,cy,cz) -> bucket. Static side persistent, dynamic side cleared/step.
    // GridHash m_staticGrid, m_dynamicGrid;

    // // solve
    // std::vector<ContactConstraint> m_constraints;
    // std::unordered_map<uint64_t, CachedImpulse> m_warmCache;   // per-contact, not per-island
    // std::vector<Island> m_islands;                             // union-find result, rebuilt/step

    

    // PhysicsManager() = default;
    // PhysicsManager(const PhysicsManager&) = delete;
    // PhysicsManager& operator=(const PhysicsManager&) = delete;
};


// Adaptive early-out — few lines, immediately makes resting scenes cheap. Do this first, today.
// Symmetric sweep + keep precomputed masses — cheap quality bumps.
// TGS soft-step — the structural win; fewer iterations, drops the position pass, better stability. The one larger investment worth making.
// Block friction / SoA / SIMD — incremental, profile-driven.
// Grid-coloring inside an oversized island — only if one island dominates after all the above.
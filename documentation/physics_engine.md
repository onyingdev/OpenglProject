# Physics Engine & Rigid Body — Reference

A from-scratch impulse-based 3D rigid-body engine built into the component system. This document
covers the architecture, every configurable knob, the solver pipeline, usage examples, and a
**stability notes / research directions** section (the known weak points and where to improve them).

Source files:
- [include/Components/Physics/RigidBodyComponent3D.h](../include/Components/Physics/RigidBodyComponent3D.h) — the body, the solver, the world (`stepWorld`).
- [include/Components/Physics/ColliderComponent3D.h](../include/Components/Physics/ColliderComponent3D.h) — a body's collision surface (physics).
- [include/Core/Components/ColliderShapes.h](../include/Core/Components/ColliderShapes.h) — shape geometry, SAT, contact manifolds.
- [include/Components/Physics/HurtBoxComponent3D.h](../include/Components/Physics/HurtBoxComponent3D.h) — gameplay overlap triggers (separate, lighter).
- [include/Core/Engine/Engine.h](../include/Core/Engine/Engine.h) — the fixed-timestep driver.

---

## 1. Overview

The engine is **impulse-based** with **semi-implicit (symplectic) Euler** integration on a
**fixed timestep** (`1/60 s`). It supports two per-body simulation modes — **Simple** (linear only)
and **Complex** (full angular dynamics) — sharing one solver. Collision is **narrowphase-only**
(no broadphase yet): every dynamic body is tested against every collider each step.

Key design choices:
- **Component-driven & data-first.** A physics object is a GameObject with a `Transform`, a
  `Collider`, optionally a `Mesh`, and a `RigidBody3D`. Everything is authored in JSON.
- **Two worlds.** *Dynamic* bodies (in `dynamicBodies()`) integrate and drive resolution. *Static*
  geometry — bodies with `"static": true`, **plus bare colliders that have no RigidBody at all** —
  is immovable and only collided against.
- **One authoritative transform.** The body, its collider, and its mesh all follow the owner
  `TransformComponent3D` (position + a quaternion `Orientation`); the integrator writes back to it,
  so rendering and collision always track the physics pose.
- **Double-dispatch shapes.** Shapes answer two parallel queries: `intersects()` → `bool` (the
  cheap gameplay/trigger path) and `collide()` / `manifold()` → `Contact` (the physics path).

### Required components for a physics body

```
GameObject
 ├─ Transform        (position / rotation / scale — the pose authority)
 ├─ Collider         (the collision surface; REQUIRED by RigidBody3D)
 ├─ Mesh             (optional — for rendering; physics works without it)
 └─ RigidBody3D      (mass + dynamics)
```

A `RigidBody3D` **throws** if there's no `Collider` on the same object, and warns (but runs) if
there's no `Mesh`.

---

## 2. The simulation step (`RigidBodyComponent3D::stepWorld`)

The engine accumulates real frame time and runs physics in constant `1/60 s` slices
([Engine.h](../include/Core/Engine/Engine.h), `kFixedDt`, capped at `kMaxSubSteps = 5` catch-up
steps to avoid a spiral of death). Each `stepWorld(dt)` runs these stages in order:

1. **Integrate** (parallel) — apply gravity/forces, advance velocity and pose for every awake
   dynamic body. Semi-implicit Euler. Complex bodies also integrate the orientation quaternion.
2. **Sync shapes** (parallel) — refresh each collider's world orientation (OBB axes, plane normal).
3. **Detection / narrowphase** (parallel) — each dynamic body sweeps the whole collider registry
   and stores its contact manifolds in its own scratch row (`m_contacts`). `manifold()` is `const`
   and reads start-of-step poses, so this parallelizes safely (mirrors `HurtBox::tickWorld`).
4. **Resolution** (serial) — **global sequential impulse**: the iteration loop is the *outermost*
   loop and cycles over *every* contact each of the 8 passes, so a body's multiple manifolds
   (e.g. resting on the ground *and* holding a box on top) converge together instead of fighting.
5. **Sleep pass** (parallel) — bodies that have been supported and below the speed thresholds for
   `0.4 s` are put to sleep (frozen, skipped) until disturbed.
6. **Lifecycle timers** (serial) — advance TTL / freeze clocks; freeze bodies in place; queue
   despawns.
7. **Despawn drain** — remove TTL-expired GameObjects from their scene (safe point, end of step).

Parallel stages use OpenMP and only kick in above 256 bodies (`#pragma omp parallel for if(...)`),
so small scenes stay serial and pay no thread overhead; without OpenMP it all degrades to plain loops.

---

## 3. `RigidBody3D` — the body

### 3.1 JSON fields

| Field | Type | Default | Meaning |
|---|---|---|---|
| `mass` | float | `1.0` | Mass in kg. `<= 0` ⇒ infinite mass (immovable). |
| `simulation` (or `mode`) | `"simple"`/`"complex"` | `"simple"` | Linear-only vs. full angular dynamics. |
| `static` | bool | `false` | Immovable world geometry: never integrates, infinite mass. |
| `useGravity` | bool | `true` | Apply `gravity` each step. |
| `gravity` | vec3 | `[0,-9.81,0]` | Per-body gravity vector. |
| `friction` | float | `0.5` | Coulomb (sliding) coefficient. Pairs combine by geometric mean. |
| `rollingFriction` | float | `0.05` | Rolling resistance — lets rollers (spheres) actually stop (Complex). |
| `drag` | float | `0.1` | Linear velocity damping per second. |
| `angularDrag` | float | `0.05` | Angular velocity damping (Complex only). |
| `restitution` | float | `0.3` | Bounciness `[0,1]`. Only applies above an impact-speed threshold (see §6). |
| `centerOfMass` | vec3 | `[0,0,0]` | Local CoM offset (force application / torque arm). |
| `initialVelocity` | vec3 | `[0,0,0]` | Starting linear velocity. |
| `initialAngularVelocity` | vec3 | `[0,0,0]` | Starting angular velocity (Complex). |
| `allowSleep` | bool | `true` | May this body sleep when it comes to rest? |
| `ttl` | float | `-1` | Seconds to live, then despawn. `<= 0` ⇒ permanent. |
| `ttlStart` | `"spawn"`/`"contact"` | `"spawn"` | When the TTL clock begins. |
| `freezeAfter` | float | `-1` | Seconds of free sim, then freeze static. `<= 0` ⇒ never. |
| `freezeStart` | `"spawn"`/`"contact"` | `"spawn"` | When the freeze clock begins. |

### 3.2 Simulation modes

- **Simple** — linear dynamics only: gravity, drag, forces/impulses, and contact resolution at the
  body center. Inverse inertia is zero, so the shared solver collapses to pure linear response.
  Cheap; the right default for most gameplay objects (pickups, projectiles you don't need to tumble).
- **Complex** — adds an inverse-inertia tensor, torque, angular velocity, quaternion integration,
  and **contact impulses applied at the contact point** so collisions/friction induce spin, rolling,
  and tumbling. Required for boxes that should tip and spheres that should roll.

Inertia tensors are computed from the collider shape × mass:
- Box / OBB: `I = (1/12) m (·)` per axis from the full extents.
- Sphere / Ball: `I = (2/5) m r²`.
- Plane (and any Simple/Static body): zero ⇒ no rotational response.

### 3.3 Examples

A complex, tumbling 1 m crate (this is the spawner prefab,
[rigid_box_ttl.json](../assets/gameObjects/rigid_box_ttl.json)):

```jsonc
{ "name": "Crate", "components": [
  { "type": "Transform", "position": [0, 5, 0] },
  { "type": "Mesh",     "model": "assets/models/1meterCube/cube1m.obj" },
  { "type": "Collider", "shape": { "kind": "obb", "halfExtents": [0.5, 0.5, 0.5] } },
  { "type": "RigidBody3D", "mass": 2.0, "restitution": 0.1, "friction": 1.5,
    "drag": 0.05, "simulation": "complex" }
]}
```

A rolling ball:

```jsonc
{ "type": "Collider",   "shape": { "kind": "sphere", "radius": 0.5 } },
{ "type": "RigidBody3D", "mass": 1.0, "restitution": 0.4, "rollingFriction": 0.1,
  "simulation": "complex" }
```

The ground — a **bare collider, no RigidBody** ⇒ implicit static barrier
([playground.json](../assets/gameObjects/playground.json)):

```jsonc
{ "type": "Collider", "shape": { "kind": "plane", "normal": [0,1,0], "infinite": true } }
```

### 3.4 Force / impulse API (for scripts)

All of these wake the body (a sleeping body resumes simulating):

| Method | Effect |
|---|---|
| `AddForce(vec3 f)` | Accumulate a continuous force (applied over the step, scaled by mass). |
| `AddImpulse(vec3 j)` | Instant velocity change (`Δv = j / m`). |
| `AddTorque(vec3 t)` | Accumulate torque (Complex). |
| `AddForceAtPosition(vec3 f, vec3 worldPos)` | Force at a point ⇒ linear force **+** torque about the CoM. |
| `AddImpulseAtPoint(vec3 P, vec3 worldPos)` | Impulse at a point ⇒ changes linear **and** angular velocity. |
| `SetVelocity(vec3 v)` | Hard-set linear velocity. |
| `wake()` / `isAsleep()` / `isFrozen()` | Sleep/freeze control & queries. |
| `inverseMass()` | `1/m` (0 for static/frozen). |

Example — a script launching the body forward on a key (the spawner uses `SetVelocity` similarly):

```cpp
if (auto* rb = owner->getComponent<RigidBodyComponent3D>())
    rb->AddImpulse(forward * 10.0f);          // a shove
    // rb->AddForceAtPosition(up * 50.0f, hitPoint);  // off-center → also spins it
```

### 3.5 Sleeping (rest state)

To stop resting bodies from jittering forever and to keep settled piles cheap, a body **sleeps**
when, *measured after resolution*, it has been **supported** (had a contact) **and** below both
speed thresholds continuously for `kSleepDelay`:

| Constant | Value | Meaning |
|---|---|---|
| `kLinSleepSq` | `0.05²` | Linear speed must be `< 0.05 m/s`. |
| `kAngSleepSq` | `0.06²` | Angular speed must be `< 0.06 rad/s`. |
| `kSleepDelay` | `0.4 s` | How long both must hold (timer resets if either fails). |

A sleeping body skips integration and isn't a resolution driver. It **wakes** when a force/impulse
is applied or when an awake body contacts it. Set `"allowSleep": false` to keep a body always live.
(There are **no sleep islands**: a sleeping body whose *dynamic* support is later removed won't wake
itself — fine for resting on static ground, the common case.)

### 3.6 Lifecycle timers (TTL + freeze)

Two independent, optional timers make spawned bodies modular. Each starts counting either on
**spawn** or on **first contact**.

- **TTL** — when it elapses the GameObject is **despawned** (removed from its scene at the end of the
  step). `<= 0` ⇒ permanent.
- **Freeze window** — when it elapses the body becomes **completely static**: stopped dead, infinite
  mass, no integration, but **still a solid collider** other bodies land and rest on. It never wakes
  again (`wake()` is ignored once frozen). `<= 0` ⇒ never freezes.

Example — spawned cube that locks solid 5 s after it lands, and lives forever:

```jsonc
{ "type": "RigidBody3D", "mass": 2.0, "simulation": "complex",
  "freezeAfter": 5.0, "freezeStart": "contact",
  "ttl": -1, "ttlStart": "spawn" }
```

Example — a projectile that despawns 3 s after it first hits something:

```jsonc
{ "type": "RigidBody3D", "mass": 1.0, "simulation": "simple",
  "ttl": 3.0, "ttlStart": "contact" }
```

Implementation notes: freeze reuses the existing `m_invMass == 0` / `m_asleep` fast-path skips, so
frozen bodies cost almost nothing but stay in the dynamic registry (so their TTL can still elapse).
Despawn is deferred to the end of `stepWorld` where mutating the scene is safe.

---

## 4. Colliders & shapes

A `Collider` owns a `Shape` and a layer/mask, and registers into a global registry the solver
sweeps. **A Collider with no RigidBody is implicit static geometry** (walls, floors).

### 4.1 Shape kinds (`"shape": { "kind": ... }`)

| `kind` | Params | Notes |
|---|---|---|
| `box` | `halfExtents:[x,y,z]` | **AABB** — axis-aligned, ignores rotation. Cheapest. |
| `obb` | `halfExtents:[x,y,z]` | **Oriented box** — rotates with the Transform; SAT-resolved (~20–50× an AABB). Use for tumbling crates. |
| `sphere` | `radius` | Rotation-invariant. |
| `ball` | `radius` | **Oriented sphere** — same collision as `sphere` but carries orientation (for spin visualization). |
| `plane` | `normal:[x,y,z]`, `infinite:bool`, `halfExtents:[u,v]` | Half-space; finite planes use a 2D bounds test. |

### 4.2 Layers & masks

A pair interacts only if `(a.Layer & b.Mask) && (b.Layer & a.Mask)`. Defaults: `Layer = 1`,
`Mask = 0xFFFFFFFF` (collide with everything). Use this to make objects pass through certain groups.

```jsonc
{ "type": "Collider", "layer": 2, "mask": 5, "shape": { "kind": "sphere", "radius": 0.5 } }
```

### 4.3 Gameplay triggers — `HurtBox` (separate system)

For "did these overlap?" gameplay logic (hits, pickups, zones) without any physical push, use
`HurtBoxComponent3D`. It sweeps its **own** registry (kept small), uses only the boolean
`intersects()` path, and fires `OnEnter` / `OnStay` / `OnExit` callbacks from a frame-to-frame diff.
No mass, no velocity, no push-out — that's the rigid body's job.

---

## 5. Collision detection internals

**Contact convention** ([ColliderShapes.h](../include/Core/Components/ColliderShapes.h)): a
`Contact { bool hit; vec3 normal; float penetration; vec3 point; }` where `normal` points **from A
toward B**, `penetration` is the overlap depth, and `point` is the world contact point.

- **Box vs box / OBB vs OBB** — 15-axis **SAT** (`satBoxesEx`): 3 faces each + 9 edge cross-products,
  returning the minimum-translation axis with a **face-over-edge bias** to stop the contact normal
  flip-flopping between a face and an edge at rest.
- **Contact manifolds** — instead of a single point, box/OBB contacts emit a *set* of points so a
  flat body rests on a balanced patch (no single-point rocking):
  - vs **plane**: `boxPlaneManifold` — one contact per penetrating corner.
  - vs **box/OBB**: `boxBoxManifold` — **Sutherland–Hodgman face clipping** (reference face vs
    incident face) for face contacts, and a single closest-point for edge-edge contacts.
  - Manifolds are capped at **8 points**.
- **Sphere/Ball** — closest-point tests; single contact.
- **OBB vs sphere/plane** — box-local closest point / projected-radius.

---

## 6. The solver (how contacts are resolved)

For each contact, `resolve()` does, per iteration:

1. **Positional correction (Baumgarte)** — push the pair apart along the normal to remove overlap:
   `corr = max(penetration − slop, 0) · percent`, split across the manifold (`posScale`) and applied
   on the first iteration only. `slop = 0.001`, `percent = 0.8` (removes ~80 % of overlap/step). Heavier
   bodies move less (scaled by inverse mass).
2. **Normal impulse with restitution** — cancel the approaching normal velocity. Restitution `e` is
   only applied for **real impacts**: below an approach speed of `kRestitutionThreshold = 0.5 m/s`
   (resting / settling under gravity) `e` is forced to 0, else slow contacts would bounce forever.
   The effective mass includes the angular term (so off-center hits spin the body).
3. **Friction** — a tangential impulse clamped to the Coulomb cone (`|jt| ≤ μ·jN`), `μ` = geometric
   mean of the two bodies' friction.
4. **Rolling resistance** — a bounded counter-torque so a rolling body actually stops (sliding
   friction alone can't stop a pure roll).

The whole manifold is solved with **8 global sequential-impulse iterations** (`kSolverIterations`).

Integration is **semi-implicit Euler**: `v += a·dt; x += v·dt` (and for Complex, the orientation
quaternion is advanced from angular velocity and renormalized).

---

## 7. Tunable constants (one-stop list for experiments)

| Constant | Where | Value | Governs |
|---|---|---|---|
| `kFixedDt` | Engine.h | `1/60 s` | Physics timestep. Smaller = more stable, costlier. |
| `kMaxSubSteps` | Engine.h | `5` | Max catch-up steps/frame. |
| `kSolverIterations` | stepWorld | `8` | Velocity-solve passes. More = stiffer stacks. |
| `slop`, `percent` | resolve | `0.001`, `0.8` | Baumgarte penetration recovery. |
| `kRestitutionThreshold` | resolve | `0.5 m/s` | Below this, bounces are suppressed (rest stability). |
| `kLinSleepSq`/`kAngSleepSq`/`kSleepDelay` | sleep pass | `0.05²`/`0.06²`/`0.4 s` | When a body sleeps. |
| manifold cap | manifold() | `8` | Max contact points per pair. |
| OpenMP threshold | stepWorld | `256` bodies | When parallel loops engage. |

---

## 8. Stability notes & research directions

This is the honest list of where the current method is weak — the useful starting points for
stability research.

**Detection**
- **No broadphase.** Narrowphase is O(N × M) every step (every dynamic body vs every collider). A
  uniform grid / sweep-and-prune / AABB tree is the big perf win and a prerequisite for large scenes.
- **No continuous collision (CCD).** Fast or thin objects can tunnel through geometry between steps,
  and deep penetration on a hard hit makes the SAT minimum-translation axis unreliable — it can flip
  to a *lateral* face, producing a sideways "pop" (this is why a fast-thrown box can shove a stack
  sideways). Fixes to research: **speculative contacts** (a contact skin that catches pairs before
  they overlap and constrains velocity so they stop at the surface — lighter than swept CCD), or
  conservative-advancement CCD.
- Detection runs against **start-of-step poses** (parallel detect-then-solve). Standard, but a body
  contacting several things at once solves against poses from before any push-out that step.

**Solver**
- **No warm-starting / persistent contacts.** Impulses aren't cached across frames, so each step
  re-converges from zero — that's *why* it needs ~8 iterations. Caching last frame's impulses
  (matching contacts by feature id) would let you drop iterations *and* stabilize stacks a lot.
- **Baumgarte position correction injects a little energy.** Split-impulse or non-linear
  Gauss-Seidel (NGS) position solving is the cleaner, more stable alternative; restitution clamping
  + sleeping currently paper over it.
- Friction/restitution use simple geometric-mean/min combines — no material-pair tables.

**Resting / stacking**
- Box-box now has a proper **clipping manifold** (good), but contacts are still not warm-started, so
  tall or interlocked stacks can stay slightly soft. Sub-stepping (smaller `kFixedDt`, or N substeps
  per frame) is the cheapest immediate lever for stack stability.
- **No sleep islands.** A sleeping body whose dynamic support is removed won't wake on its own.
- Sleep thresholds are global constants; a legitimately slow body can be frozen, and a residual
  limit-cycle just above the threshold can keep a body awake.

**Model scope**
- CoM is fixed at the shape origin (no compound/offset CoM inertia).
- No joints/constraints (hinges, springs, motors), no continuous forces beyond gravity + drag.
- Non-uniform scale on spheres uses `scale.x` only; AABB ignores rotation (use OBB when it matters).

Suggested priority for a stability pass: **sub-stepping** (immediate) → **warm-starting**
(stacks + lets you cut iterations) → **speculative contacts** (kills tunnelling + the lateral pop)
→ **broadphase** (scale) → **sleep islands**.

---

## 9. Tooling & debugging

- **`RigidBodyComponent3D::DebugPrint`** (static bool, default `false`) — when `true`, `stepWorld`
  dumps each awake body's `pos`, `|v|`, `|w|`, contact-pair count and total contact points (throttled
  ~10×/s). Zero overhead when off. Flip it in code to inspect jitter/settling.
- **Collider wireframes** — toggle with the `ToggleColliders` action (default **F1**): draws every
  collider (green) and hurtbox (red).
- **Hot reload** — the `ReloadScenes` action (default **R**) rebuilds scenes from disk; tweak JSON
  values and reload without restarting.

## 10. Runtime spawning example (`CubeSpawner`)

[cubeSpawner.h](../assets/scripts/cubeSpawner.h) spawns a prefab at the camera on a key press
(default **Space**). It loads the prefab through the normal `GameObjectLoader`, gives it a unique
name, places it at `cameraPos + forward · spawnDistance`, adds it to the target scene, and calls
`start()` (which registers the body with the physics world). With `launchSpeed > 0` it throws the
cube along the camera forward. Combined with the lifecycle timers, spawned cubes can self-clean
(`ttl`) or settle into static scenery (`freezeAfter`) — see
[rigid_box_ttl.json](../assets/gameObjects/rigid_box_ttl.json).

#pragma once
#include <glm/glm.hpp>
#include <vector>
#include <unordered_map>
#include <cstdint>
#include <cmath>

// Uniform-grid spatial hash for the broadphase. Deliberately knows NOTHING about rigid bodies — it
// stores integer body INDICES keyed by cell. The caller (which has the complete body type) computes
// each body's world AABB and feeds (index, min, max); a query returns the indices sharing any cell,
// i.e. the candidate-collision neighbours. This keeps the grid out of the component<->manager include
// cycle and makes it a plain, testable structure.
//
// SPATIAL HASH (not a dense array): cells are addressed by hashed integer coordinates, so the world
// is unbounded and only occupied cells cost memory. (A dense/chunked layer can sit on top later.)
//
// RANGE: an optional bounded active region — center + per-axis half-extents. It's a vec3 so the
// horizontal (x/z) and vertical (y) limits can differ; pass equal components for a cube. Disabled by
// default (rangeHalf huge) → every body is inserted. When enabled, bodies whose center is outside the
// box are NOT inserted, so they generate no contacts (a first, broadphase-only form of culling; full
// dormancy — skipping integration — comes with the sleep/range pass).
//
// UNBOUNDED shapes (infinite planes) are never binned (their AABB spans every cell). The caller puts
// them in `unbounded` and tests each finite body against them directly.
struct UniformGrid {
    float cellSize = 2.0f;            // ≈ largest body; tune per scene

    bool      rangeEnabled = false;
    glm::vec3 rangeCenter{0.0f};
    glm::vec3 rangeHalf{1.0e30f};     // huge until set → effectively unbounded

    // DYNAMIC partition — rebuilt every step (moving bodies).
    std::unordered_map<uint64_t, std::vector<int>> cells;
    std::vector<int> unbounded;       // infinite planes — tested against every finite body

    // STATIC partition — bodies that never move are binned ONCE and kept across frames; only
    // rebuilt when the static set changes (add/remove/freeze/range — the manager's m_staticDirty).
    // A dynamic driver queries BOTH partitions; the indices in the two are disjoint sets of `world`.
    std::unordered_map<uint64_t, std::vector<int>> staticCells;
    std::vector<int> staticUnbounded; // static infinite planes (e.g. the ground)

    // Is a body's center inside the active region? (Always true when the range is off.)
    bool inRange(const glm::vec3& center) const {
        if (!rangeEnabled) return true;
        const glm::vec3 d = glm::abs(center - rangeCenter);
        return d.x <= rangeHalf.x && d.y <= rangeHalf.y && d.z <= rangeHalf.z;
    }

    void clear()       { cells.clear(); unbounded.clear(); }              // dynamic side (every step)
    void clearStatic() { staticCells.clear(); staticUnbounded.clear(); }  // static side (only on rebuild)

    // Pack signed cell coords into one key: 21 bits/axis (±~1M cells), biased to stay non-negative.
    // Unique per cell within that range, so no two distinct cells collide into one bucket.
    static uint64_t key(int x, int y, int z) {
        const uint64_t ux = uint64_t(uint32_t(x + (1 << 20))) & 0x1FFFFFull;
        const uint64_t uy = uint64_t(uint32_t(y + (1 << 20))) & 0x1FFFFFull;
        const uint64_t uz = uint64_t(uint32_t(z + (1 << 20))) & 0x1FFFFFull;
        return ux | (uy << 21) | (uz << 42);
    }
    int cellCoord(float v) const { return int(std::floor(v / cellSize)); }

    // Register a finite body into every cell its AABB overlaps (1 cell if it fits, more if it straddles).
    void insert(int idx, const glm::vec3& mn, const glm::vec3& mx) {
        const int x0 = cellCoord(mn.x), x1 = cellCoord(mx.x);
        const int y0 = cellCoord(mn.y), y1 = cellCoord(mx.y);
        const int z0 = cellCoord(mn.z), z1 = cellCoord(mx.z);
        for (int x = x0; x <= x1; ++x)
            for (int y = y0; y <= y1; ++y)
                for (int z = z0; z <= z1; ++z)
                    cells[key(x, y, z)].push_back(idx);
    }

    // Same as insert(), but into the persistent static partition.
    void insertStatic(int idx, const glm::vec3& mn, const glm::vec3& mx) {
        const int x0 = cellCoord(mn.x), x1 = cellCoord(mx.x);
        const int y0 = cellCoord(mn.y), y1 = cellCoord(mx.y);
        const int z0 = cellCoord(mn.z), z1 = cellCoord(mx.z);
        for (int x = x0; x <= x1; ++x)
            for (int y = y0; y <= y1; ++y)
                for (int z = z0; z <= z1; ++z)
                    staticCells[key(x, y, z)].push_back(idx);
    }

    // Append the indices in every cell the AABB overlaps into `out` (WITH duplicates — a neighbour in
    // several shared cells appears several times; the caller sorts/uniques and applies the j>i rule).
    void query(const glm::vec3& mn, const glm::vec3& mx, std::vector<int>& out) const {
        const int x0 = cellCoord(mn.x), x1 = cellCoord(mx.x);
        const int y0 = cellCoord(mn.y), y1 = cellCoord(mx.y);
        const int z0 = cellCoord(mn.z), z1 = cellCoord(mx.z);
        for (int x = x0; x <= x1; ++x)
            for (int y = y0; y <= y1; ++y)
                for (int z = z0; z <= z1; ++z) {
                    auto it = cells.find(key(x, y, z));
                    if (it != cells.end()) out.insert(out.end(), it->second.begin(), it->second.end());
                }
    }

    // Same as query(), but APPENDS the static-partition occupants the AABB overlaps into `out`.
    void queryStatic(const glm::vec3& mn, const glm::vec3& mx, std::vector<int>& out) const {
        const int x0 = cellCoord(mn.x), x1 = cellCoord(mx.x);
        const int y0 = cellCoord(mn.y), y1 = cellCoord(mx.y);
        const int z0 = cellCoord(mn.z), z1 = cellCoord(mx.z);
        for (int x = x0; x <= x1; ++x)
            for (int y = y0; y <= y1; ++y)
                for (int z = z0; z <= z1; ++z) {
                    auto it = staticCells.find(key(x, y, z));
                    if (it != staticCells.end()) out.insert(out.end(), it->second.begin(), it->second.end());
                }
    }
};

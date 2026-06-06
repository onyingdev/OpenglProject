#pragma once
#include <glm/glm.hpp>
#include <nlohmann/json.hpp>

// Read a [x, y, z] JSON array into a vec3, falling back when absent/malformed.
inline glm::vec3 jsonVec3(const nlohmann::json& j, const glm::vec3& fallback) {
    if (!j.is_array() || j.size() < 3) return fallback;
    return { j[0].get<float>(), j[1].get<float>(), j[2].get<float>() };
}

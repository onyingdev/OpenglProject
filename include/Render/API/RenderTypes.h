#pragma once
#include <vector>
#include <glm/glm.hpp>

class IModel;

// Camera data the renderer needs for a frame. Passed as a POD so the renderer stays
// decoupled from CameraComponent (it never sees an engine component).
struct CameraView {
    glm::mat4 view{1.0f};
    glm::mat4 projection{1.0f};
    glm::vec3 position{0.0f};   // world-space eye position (shader viewPos)
};

// Which built-in material a draw uses. RenderItems name a material, never a concrete
// shader, so the backend owns shader selection.
enum class MaterialKind { Lit, Unlit };

// One draw submission: a model, where to put it, and how to shade it.
struct RenderItem {
    IModel*      model = nullptr;
    glm::mat4    modelMatrix{1.0f};
    MaterialKind material = MaterialKind::Lit;
    glm::vec3    unlitColor{1.0f};   // used when material == Unlit (e.g. a lamp cube)
    bool         wireframe = false;  // absorbs the old glPolygonMode toggle
};

struct DirLight {
    glm::vec3 direction{0.0f};
    glm::vec3 ambient{0.0f}, diffuse{0.0f}, specular{0.0f};
};

struct PointLight {
    glm::vec3 position{0.0f};
    glm::vec3 ambient{0.0f}, diffuse{0.0f}, specular{0.0f};
    float constant = 1.0f, linear = 0.09f, quadratic = 0.032f;
    bool  enabled = false;
};

struct SpotLight {
    glm::vec3 position{0.0f}, direction{0.0f};
    glm::vec3 ambient{0.0f}, diffuse{0.0f}, specular{0.0f};
    float cutOff = 0.0f, outerCutOff = 0.0f;
    float constant = 1.0f, linear = 0.09f, quadratic = 0.032f;
    bool  enabled = false;
};

// All lighting for a frame. The built-in lit shader supports up to 4 point lights.
struct LightSet {
    DirLight                dir;
    std::vector<PointLight> points;
    SpotLight               spot;
};

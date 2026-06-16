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
//   Reflective: environment-mapped mirror reflection of a cube map (envCubemap below).
enum class MaterialKind { Lit, Unlit, Reflective };

// One draw submission: a model, where to put it, and how to shade it.
struct RenderItem {
    IModel*      model = nullptr;
    glm::mat4    modelMatrix{1.0f};
    MaterialKind material = MaterialKind::Lit;
    glm::vec3    unlitColor{1.0f};   // used when material == Unlit (e.g. a lamp cube)
    bool         wireframe = false;  // absorbs the old glPolygonMode toggle
    // GL cube-map texture id reflected when material == Reflective (0 -> fall back to the scene
    // skybox). Plain unsigned int so this API header stays free of any GL type dependency.
    unsigned int envCubemap = 0;
};

struct DirLight {
    glm::vec3 direction{0.0f};
    glm::vec3 ambient{0.0f}, diffuse{0.0f}, specular{0.0f};
};

struct PointLight {
    glm::vec3 position{0.0f};
    glm::vec3 ambient{0.0f}, diffuse{0.0f}, specular{0.0f};
    float constant = 1.0f, linear = 0.09f, quadratic = 0.032f;
    float attenuation = 1.0f;   // numerator multiplier: >1 makes the light reach farther
    bool  enabled = false;
};

struct SpotLight {
    glm::vec3 position{0.0f}, direction{0.0f};
    glm::vec3 ambient{0.0f}, diffuse{0.0f}, specular{0.0f};
    float cutOff = 0.0f, outerCutOff = 0.0f;
    float constant = 1.0f, linear = 0.09f, quadratic = 0.032f;
    bool  enabled = false;
};

// All lighting for a frame. The built-in lit shader supports up to 4 point lights
// and up to 4 spot/cone lights (the camera flashlight is just one of the spots).
struct LightSet {
    DirLight                dir;
    std::vector<PointLight> points;
    std::vector<SpotLight>  spots;
};

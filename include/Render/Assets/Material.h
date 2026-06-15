#pragma once
#include <glm/glm.hpp>

// CPU-side PBR material (glTF 2.0 metallic-roughness model). GL-free: the loader (worker thread) fills
// it and a GPU backend consumes it, exactly like the rest of ModelData.
//
// Texture fields index into ModelData::textures (-1 = absent). The scalar factors apply *regardless* of
// whether the matching texture is present — a material can be a flat color with no maps at all, and when
// a map IS present the factor multiplies it (glTF semantics). These factors are why a Material is needed
// in addition to the per-mesh texture list: textures alone can't express baseColor/metallic/roughness/
// emissive constants or alpha mode.
//
// The PBR *shading* that consumes metallic/roughness/occlusion/emissive is deferred; for now the loader
// fills this so the data is carried, while rendering stays on the existing Blinn-Phong path (which only
// samples base color as albedo).
struct Material {
    // pbrMetallicRoughness factors
    glm::vec4 baseColorFactor    = glm::vec4(1.0f);
    float     metallicFactor     = 1.0f;
    float     roughnessFactor    = 1.0f;
    // emissive / map scales
    glm::vec3 emissiveFactor     = glm::vec3(0.0f);
    float     normalScale        = 1.0f;
    float     occlusionStrength  = 1.0f;

    // Indices into ModelData::textures (-1 = none).
    int baseColorTex         = -1;   // sRGB
    int metallicRoughnessTex = -1;   // linear (G=roughness, B=metallic; R=occlusion if ORM-packed)
    int normalTex            = -1;   // linear
    int occlusionTex         = -1;   // linear
    int emissiveTex          = -1;   // sRGB

    enum AlphaMode { Opaque, Mask, Blend };
    AlphaMode alphaMode   = Opaque;
    float     alphaCutoff = 0.5f;
    bool      doubleSided = false;
};

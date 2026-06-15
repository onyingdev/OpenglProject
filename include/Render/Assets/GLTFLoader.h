#pragma once
#include "Render/Assets/ModelData.h"   // backend-agnostic CPU parse output
#include <string>

// glTF 2.0 loader (tinygltf-backed). Mirrors Model::parse's contract: read + decode a .gltf/.glb file
// into a backend-agnostic ModelData touching NO graphics API, so it can run on a worker thread. The GL
// upload stays in Model (uploadFrom). This header is a pure declaration — the tinygltf implementation
// (which pulls in its own bundled json.hpp) is isolated in src/Core/gltf_loader.cpp so it never collides
// with the project's nlohmann json or the stb_image implementation compiled elsewhere.
namespace gltf {

// True if `path` looks like a glTF asset (.gltf text or .glb binary), case-insensitive.
bool isGLTF(const std::string& path);

// Parse a glTF/glb model into ModelData. Node transforms are baked into vertex positions/normals (the
// engine's Mesh has no per-node transform). On failure, returns ModelData with valid == false.
// flipUVs flips V on the texcoords (off by default — glTF's top-left UV origin already matches a
// non-flipped texture upload). gamma is recorded on the ModelData for the upload stage, as with Assimp.
ModelData Load(const std::string& path, bool gamma = false, bool flipUVs = false);

}  // namespace gltf

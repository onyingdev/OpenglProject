# Model Loading & glTF Import — Reference

How the engine loads 3D models, with a dedicated **glTF 2.0** import path alongside the existing
Assimp loader, sRGB-correct color management, and a PBR material model. Loading is **data-first**: a
renderable object is a GameObject with a `Transform` and a `Mesh` component, authored in JSON.

Source files:
- [include/Render/model.h](../include/Render/model.h) — `Model`: parse (CPU) + GPU upload, format dispatch.
- [include/Render/Assets/GltfLoader.h](../include/Render/Assets/GltfLoader.h) — the dedicated glTF 2.0 importer.
- [include/Render/Assets/ModelData.h](../include/Render/Assets/ModelData.h) — backend-agnostic CPU asset data.
- [include/Render/Assets/Material.h](../include/Render/Assets/Material.h) — PBR (metallic-roughness) material model.
- [include/Components/Rendering/MeshComponent.h](../include/Components/Rendering/MeshComponent.h) — the `"Mesh"` component.
- [include/Render/Assets/AssetManager.h](../include/Render/Assets/AssetManager.h) — async + streamed GPU upload.

---

## 1. Quick start

Loading a glTF is **transparent** — there is no special API. `Model::parse` dispatches by file
extension, so you load a `.gltf` exactly like an `.obj`/`.fbx`: point a model path at it.

```json
{ "type": "Mesh", "model": "assets/models/helmet/DamagedHelmet.gltf" }
```

Supported formats:
- **glTF 2.0** (`.gltf` + `.bin` + textures) → the dedicated [GltfLoader](../include/Render/Assets/GltfLoader.h).
- **Everything else** (`.obj`, `.fbx`, …) → Assimp, unchanged.

---

## 2. The `"Mesh"` component — JSON

```json
{ "type": "Mesh",
  "model":    "assets/models/helmet/DamagedHelmet.gltf",
  "async":    true,        // optional — parse off-thread, pop in once uploaded
  "material": "lit",       // "lit" (default, scene lighting) | "unlit" (flat color)
  "color":    [1, 1, 1] }  // only used when material is "unlit"
```

| Key | Default | Meaning |
|---|---|---|
| `model` | `""` | Path to the model file. `.gltf`/`.glb` → glTF importer, else Assimp. |
| `async` | `false` | `true`: load on a worker thread; the object renders nothing until the upload finishes (no load-time hitch). Prefer for heavy models / runtime spawns. |
| `material` | `"lit"` | `"lit"` shades with scene lights; `"unlit"` draws a flat `color` (lamps/markers). |
| `color` | `[1,1,1]` | Flat color for `"unlit"`. |
| `flipUVs` | `false` | Vertical UV flip. **Leave `false` for glTF** (see §5). Only relevant to some Assimp assets. |

> **Do not set `"flipUVs": true` for glTF.** glTF authoring already matches OpenGL once images are
> uploaded un-flipped (the default); flipping inverts the UVs incorrectly.

---

## 3. Loading from C++

```cpp
// Shared upload, cached per path (what the "Mesh" component uses) — repeated loads of the same
// path reuse one GPU upload:
auto model = ModelCache::get("assets/models/helmet/DamagedHelmet.gltf");

// Async: worker-thread parse + streamed GPU upload. Poll the handle, promote when ready.
auto handle = AssetManager::instance().request("assets/models/helmet/DamagedHelmet.gltf");
// ... each frame: if (handle->ready) myModel = handle->model;

// Fresh, un-shared instance (direct load):
auto comp = std::make_shared<MeshComponent>("assets/models/helmet/DamagedHelmet.gltf");
```

All three run the same two-stage pipeline: a **GL-free `parse()`** (file read + JSON/Assimp decode +
image decode, safe on a worker thread) producing a `ModelData`, then a **render-thread upload** into
GPU meshes/textures (optionally streamed across frames within a byte budget).

---

## 4. File layout

A `.gltf` references its buffer and images by **relative URI**, resolved against the `.gltf`'s own
directory. Keep them together:

```
assets/models/helmet/
  DamagedHelmet.gltf          <- the file you point "model" at
  DamagedHelmet.bin           <- geometry/animation buffer
  Default_albedo.jpg          <- textures referenced by the .gltf
  Default_normal.jpg
  Default_metalRoughness.jpg
  Default_emissive.jpg
  Default_AO.jpg
```

A `textures/` subfolder works too — whatever relative paths the `.gltf` itself uses. Windows
backslashes and `%20`-style URI escapes are handled. Base64 `data:` buffers/images and
bufferView-embedded images are also supported.

If a texture is missing, the console prints `Texture failed to load at path: <resolved path>` and the
mesh shows the **magenta fallback** there — usually a sign the `.bin`/image isn't next to the `.gltf`.

---

## 5. Color management (sRGB)

The renderer now works in **linear light** with a single gamma encode at present:

- **Color textures** (base color, emissive) upload as `GL_SRGB8`/`GL_SRGB8_ALPHA8` — the GPU
  linearizes them on sample.
- **Data textures** (normal, metallic-roughness, occlusion) upload as **linear** — never gamma-touched.
- Light colors, unlit colors, and debug-gizmo colors authored in JSON are treated as display-space
  sRGB and **linearized** before lighting.
- The present pass (`msaa_post.fs` / `fxaa_s.fs`) applies the final `pow(1/2.2)` once for display.

This applies to **all** models (Assimp too), so existing OBJ/FBX scenes stay correct under the new
pipeline. The net effect on an existing scene is a (correct) shift toward more natural lighting; flat
unlit/gizmo colors round-trip unchanged.

---

## 6. What glTF imports (and what it doesn't, yet)

**Imported (glTF 2.0 core, metallic-roughness):**
- Geometry: `POSITION`, `NORMAL`, `TEXCOORD_0`, `TANGENT` (+ indices, u8/u16/u32). Tangents are
  generated when a normal map is present but `TANGENT` is absent.
- Buffers: external `.bin`, base64 `data:` URIs.
- Images: external files, `data:` URIs, bufferView-embedded.
- Materials: `baseColor`, `metallicRoughness`, `normal`, `occlusion`, `emissive` maps + their factors
  (baseColorFactor, metallic/roughnessFactor, emissiveFactor, normalScale, occlusionStrength),
  `alphaMode`/`alphaCutoff`, `doubleSided`. Carried on `Model::materials`.

**Not yet (logged and skipped):** the `.glb` binary container (use the separate `.gltf` + `.bin`
form), Draco compression, animation, skins, morph targets, cameras, sparse accessors, secondary UV
sets, and KHR material extensions beyond core metallic-roughness.

---

## 7. Rendering note (interim Blinn-Phong)

The PBR **shading** (Cook-Torrance BRDF, IBL) is a separate, later step. Today, glTF models render
through the existing Blinn-Phong shader: **base color is shown correctly as albedo**, and the
metallic-roughness/normal/occlusion/emissive maps are loaded and bound (ready for the PBR shader) but
not yet shaded. So a glTF appears correctly textured and lit, just not fully physically shaded until
the PBR pass lands. glTF has no specular map, so the Blinn-Phong specular term falls back to a default
(pre-existing behavior).

---

## 8. Test assets

The Khronos `vendor/glTF` repo is the **specification** (spec + JSON schema + a few extension
examples) — it ships no standard PBR sample models. For end-to-end testing grab one from KhronosGroup
**glTF-Sample-Assets**:
- `BoxTextured` — minimal smoke test (geometry + one base-color texture).
- `DamagedHelmet` — full PBR set (base color + metallic-roughness + normal + occlusion + emissive).

…or use your own `.gltf` + `.bin` + textures.

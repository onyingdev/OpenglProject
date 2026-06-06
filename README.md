# OpenGL Project - Game Engine

A 3D graphics engine based on OpenGL written in C++, with a flexible component system for building graphical and interactive applications similar to Unity and Godot.

## Features

- **Decoupled Rendering Layer (in development)** - Adapter layer allows to swap between OpenGL, Vulkan and Other rendering APIs
- **Component System** - Component Pattern used for creating GameObjects
- **3D Model Loading** - Imports OBJ, FBX, and other model formats via ASSIMP (need expansion and refinement)
- **Input System** - Keyboard and mouse handling with Actions
- **Custom Shaders** - System for loading and compiling shaders (vertex/fragment)
- **Scripts System** - Extensible Script system for behavior logic.
- **Internal Scaling** - Rendering at a fixed resolution with scaling to the window (letterbox/pillarbox)
- more features no yet defined...

## Requirements

### System Dependencies

- **Linux (Fedora 44)**:
  ```bash
  sudo dnf install glfw-devel assimp-devel
  ```

- **Linux (Debian/Ubuntu)**:
  ```bash
  sudo apt install libglfw3-dev libassimp-dev
  ```

- **macOS** (with Homebrew):
  ```bash
  brew install glfw assimp
  ```

- **Windows** (MSYS2 UCRT64):
  ```bash
  pacman -S mingw-w64-ucrt-x86_64-glfw mingw-w64-ucrt-x86_64-assimp
  ```

### Development Tools

- **Compiler**: GCC/G++ (C++17 or higher)
- **Build**: Make
- **IDE (optional)**: CLion, VSCode

## Building and Running

### Build

```bash
make
```

`make recompile` rebuilds only `main.o` (and relinks), skipping the vendored
`glad`/`shader`/`asset_loader`/`input_manager` objects — use it when you only
touched header-only code under `include/` or `assets/scripts/`.

### Run

```bash
./main
```

### Clean

```bash
make clean
```

### Include paths

Sources reference headers by **logical path**, not relative `../..` chains:

```cpp
#include "Core/Components/GameObject.h"   // engine header   (-Iinclude)
#include "Components/Rendering/MeshComponent.h"
#include <glm/glm.hpp>                     // vendored library (-isystem vendor/glm)
```

These roots are declared once in the `Makefile` (`INCLUDES = -I. -Iinclude
-isystem vendor/glad -isystem vendor/glm -isystem vendor/GLFW/include`) and
mirrored for the editor in `.vscode/c_cpp_properties.json`. Moving a folder only
updates those roots — never the include lines in every file.

## Project Structure

```
.
├── assets/                       # Game content
│   ├── models/                   # 3D models (OBJ/FBX) with their textures + materials
│   ├── shaders/                  # GLSL shaders (*.vs, *.fs)
│   ├── textures/                 # Standalone textures
│   └── scripts/                  # Behavior scripts (header-only C++)
├── include/                      # Engine headers (mostly header-only)
│   ├── Components/                # Attachable components, grouped by subsystem
│   │   ├── Rendering/            #   MeshComponent, CameraComponent
│   │   ├── Physics/              #   ColliderComponent3D, RigidBodyComponent3D
│   │   ├── Input/                #   InputComponent
│   │   ├── Gameplay/             #   CharacterComponent3D
│   │   ├── Audio/                #   SoundComponent3D
│   │   └── World/                #   TerrainComponent3D
│   ├── Core/                     # Engine foundation
│   │   ├── Components/           #   Base component types + scene graph
│   │   │   ├── Component.h       #     base class every component inherits
│   │   │   ├── GameObject.h      #     component container
│   │   │   ├── SpatialComponent3D.h  # base for anything with a transform
│   │   │   ├── TransformComponent3D.h # position/rotation/scale (+ plain Transform POD)
│   │   │   ├── Scene.h           #     loadable/unloadable object package
│   │   │   └── script.h         #     Script base class
│   │   └── Engine/              #   Engine services
│   │       ├── input_manager.h  #     action → key mapping
│   │       ├── lib_asset_loader.h #   texture/asset loading
│   │       ├── exceptions.h     #     engine exceptions / warnings
│   │       └── stb_image.h      #     image decode (vendored)
│   └── Render/                   # Rendering primitives (Shader, Model, Mesh)
├── src/                          # .cpp implementations
│   ├── Core/                     #   input_manager, lib_asset_loader
│   └── Render/                   #   shader_s
├── vendor/                       # Third-party deps (glad, GLFW, glm)
├── main.cpp                      # Entry point (engine + app, being decoupled)
├── Makefile                      # Build script (g++, OpenMP, -I include roots)
└── README.md                     # This file
```

## GameObjects and Components

The architecture is based on the Component Pattern (by Bob Nystrom). A GameObject is defined as a class that is composed primarily by components. Components are individual pieces that can define a characteristic, attribute or behaviour and they can be built from other components but always have unique piece of code that changes their attributes.

### Core / Foundation (`include/Core/Components`)

The foundation types are required for the engine to work; concrete components inherit from them.

- **Component** - The template all components inherit from (`start()` / `update(dt)` lifecycle).
- **GameObject** - Container that owns components and resolves them by type.
- **SpatialComponent3D** - Base for any component that can be placed in the world. Holds a local `Transform` plus per-axis source pointers that select between the local copy and the owner's transform.
- **TransformComponent3D** - Position, rotation (Euler degrees), and scale. Backed by a plain `Transform` POD (no vtable/owner overhead) so it can be embedded cheaply.
- **Script** - Base class for behavior scripts.
- **Scene** - A package of objects that can be loaded/unloaded.

### Components by Subsystem (`include/Components`)

Concrete, attachable components, grouped by the engine subsystem that drives them.

- **Rendering/**
  - **MeshComponent** - 3D model rendering.
  - **CameraComponent** - Perspective camera (produces view/projection matrices).
- **Physics/**
  - **ColliderComponent3D** - Collision shapes (AABB box, sphere) with enter/stay/exit triggers.
  - **RigidBodyComponent3D** - Physics body *(planned)*.
- **Input/**
  - **InputComponent** - Pipes the `InputManager` into per-action callbacks.
- **Gameplay/**
  - **CharacterComponent3D** - Marks a GameObject as a playable character (aggregates Mesh + Input).
- **Audio/**
  - **SoundComponent3D** - 3D spatial audio source *(planned)*.
- **World/**
  - **TerrainComponent3D** - Procedural terrain / LOD maps *(planned)*.

### Scripts (`assets/scripts`)

- **CameraController** - Free-camera movement and mouse look.
- **CameraFollow** - Camera tracks a target with a positional/rotational offset.
- **CharacterController** - Drives a character from input actions.
- **SpinModel** - Automatic rotation on an axis.

## Basic Usage

### Create a GameObject with Components

```cpp
auto myObject = std::make_unique<GameObject>();
myObject->Name = "MyActor";

// Add components
myObject->addComponent<TransformComponent>()->Position = {0, 0, 0};
myObject->addComponent<MeshComponent>("assets/models/mymodel.obj");
myObject->addComponent<MyScript>();  // Custom script

// Initialize
myObject->start();
```

### Create a Custom Script

Inherit from `Script` and implement `start()` and `tick(dt)`:

```cpp
class MyScript : public Script {
    TransformComponent* tf;
    
public:
    void start() override {
        tf = owner->getComponent<TransformComponent>();
        //place this check if the component is mandatory
        if (!tf) throw MissingComponentException("MyScript", "TransformComponent");
    }
    
    void tick(float dt) override {
        // Game logic here
        tf->Position.y += 1.0f * dt;
    }
};
```

### Create a Custom Shader

**assets/shaders/custom.vs** (Vertex Shader):
```glsl
#version 330 core
layout(location = 0) in vec3 position;

uniform mat4 view;
uniform mat4 projection;
uniform mat4 model;

void main() {
    gl_Position = projection * view * model * vec4(position, 1.0);
}
```

**assets/shaders/custom.fs** (Fragment Shader):
```glsl
#version 330 core
out vec4 FragColor;

void main() {
    FragColor = vec4(1.0, 0.0, 0.0, 1.0);  // Red
}
```

## Runtime Controls

Default bindings (see `main.cpp` → `InputManager::BindAction`). These are a work in progress.

| Key | Action |
|-------|--------|
| ↑ | Move forward |
| ↓ | Move backward |
| ← | Move left |
| → | Move right |
| N | Rotate left |
| M | Rotate right |
| F | Toggle flashlight |
| Mouse | Look around |
| ESC | Exit |

## Resolution Configuration

In `main.cpp`:

```cpp
// Window resolution
unsigned int SCR_WIDTH = 1920;
unsigned int SCR_HEIGHT = 1080;

// Internal rendering resolution (independent of window)
const int RENDER_WIDTH  = 1920;
const int RENDER_HEIGHT = 1080;
```

## Technical Details

### Offscreen Rendering

The project uses Framebuffer Objects (FBO) to:
- Render at a fixed internal resolution
- Scale to window resolution without affecting performance
- Maintain a constant aspect ratio (letterbox/pillarbox)

### Input System

The `InputManager` maps actions to keys:

```cpp
InputManager input(window);
input.BindAction("Jump", GLFW_KEY_SPACE);
input.IsActionPressed("Jump");
```

### Component System

Based on C++ templates for static types:

```cpp
// Get component (nullptr if it doesn't exist)
auto* camera = gameObject->getComponent<CameraComponent>();

// Add component
auto* mesh = gameObject->addComponent<MeshComponent>("path/to/model.obj");
```

### Collision Sweep

`ColliderComponent3D::tickWorld()` runs once per frame over all live colliders,
testing shapes via double dispatch (box/sphere) and firing OnEnter/OnStay/OnExit
callbacks. The broadphase is currently O(N²) and parallelized with OpenMP
(`-fopenmp`); a spatial broadphase is a planned improvement.

## Troubleshooting

### "Failed to initialize GLAD"
Make sure OpenGL 3.3 or higher is available on your GPU.

### "Texture failed to load"
Verify that texture paths are relative to the execution directory.

### Compilation errors with includes
Confirm the `-I` roots in the `Makefile` match your layout, then run `make clean` and rebuild.

## License

This project is available under the MIT license.

## Development Notes

- The component system uses `std::unique_ptr` for automatic memory management
- Scripts run in order of addition
- TransformComponent3D uses Euler angles (degrees) for rotation
- All shaders must be in GLSL 3.30 format

</content>

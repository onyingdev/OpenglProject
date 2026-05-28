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

### Run

```bash
./main
```

### Clean

```bash
make clean
```

## Project Structure

```
.
├── assets/                 # Game content
│   ├── models/            # 3D models (OBJ, FBX, etc.)
│   ├── shaders/           # Shader files (*.vs, *.fs)
│   ├── textures/          # Textures and images
│   └── scripts/           # Behavior scripts (C++)
├── include/               # Public headers
│   ├── Components/        # Reusable components
│   ├── Core/              # Base system (GameObject, Component, Input)
│   └── Render/            # Rendering (Shader, Model, Mesh)
├── src/                   # Implementation
│   ├── Core/              # Core implementation
│   └── Render/            # Render implementation
├── vendor/                # External dependencies
│   ├── glad/              # OpenGL loader
│   ├── GLFW/              # Window and input
│   └── glm/               # Vector math
├── main.cpp               # Entry point
├── Makefile               # Build script
└── README.md              # This file
```

## GameObjects and Components

The architecture is based on the Component Pattern (by Bob Nystrom). A GameObject is defined as a class that is composed primarily by components. Components are individual pieces that can define a characteristic, attribute or behaviour and they can be built from other components but always have unique piece of code that changes their attributes.

### Core Components

The Core Components forms the basis for all components and are required by the software to work.

- **Component** - The template for all components, they inherit from here.
- **TransformComponent3D** - Position, rotation (Euler), and scale in 3D
- **Script** - The components for scripting
- **Scene** - This component contains objects like a package that can be loaded/unloaded. 
- **InputComponent** - Keyboard and mouse input handling

### Primary Components

The Primary Components are directly derived from the Core Components, these are used make programming easier

- **SpatialComponent (requires refactor)** - The spatial component is the most important component of the primary, since they are inherited from this one. Basically any component that has the attribute to be able to be place anywhere is a Spatial Component.
- **CameraComponent** - Camera with perspective projection
- **MeshComponent** - 3D model rendering
- **ColliderComponent3D** - 3D collisions
- **RigidBodyComponent3D** - Physics (rigid body)
- **SoundComponent3D** - 3D spatial audio

### Additional Components

Additional Components derived from other components, makes programming much easier

- **TerrainComponent3D** - Procedural terrain/LOD maps
- **CharacterComponent3D** - Defines a GameObject as a playable character

### System Scripts

- **CameraController** - Camera movement with WASD and mouse look
- **SpinModel** - Automatic rotation on the Y axis

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

Currently this will change

| Key | Action |
|-------|--------|
| W | Move forward |
| S | Move backward |
| A | Move left |
| D | Move right |
| Q | Move up |
| E | Move down |
| Mouse | Rotate view |
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

## Troubleshooting

### "Failed to initialize GLAD"
Make sure OpenGL 3.3 or higher is available on your GPU.

### "Texture failed to load"
Verify that texture paths are relative to the execution directory.

### Compilation errors with includes
Run `make clean` and rebuild.

## License

This project is available under the MIT license.

## Development Notes

- The component system uses `std::unique_ptr` for automatic memory management
- Scripts run in order of addition
- TransformComponent3D uses Euler angles (degrees) for rotation
- All shaders must be in GLSL 3.30 format

## Next Steps

- [ ] Implement a more complete physics system
- [ ] Add GUI/HUD
- [ ] Optimize geometry culling
- [ ] Particle system
- [ ] Dynamic lighting

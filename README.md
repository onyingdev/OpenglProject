# OpenGL Project - Game Engine

Un motor gráfico 3D basado en OpenGL escrito en C++, con un sistema de componentes flexible para construir aplicaciones gráficas e interactivas.

## Características

- **Renderizado 3D con OpenGL 3.3 Core** - Soporte para shaders y rendering moderno
- **Sistema de Componentes** - Arquitectura GameObject/Component para organizar código
- **Carga de Modelos 3D** - Importa modelos OBJ, FBX y otros formatos vía ASSIMP
- **Sistema de Entrada** - Manejo de teclado y ratón con AcciónActions
- **Shaders Personalizados** - Sistema de carga y compilación de shaders (vertex/fragment)
- **Cámara 3D** - Componente de cámara con proyección perspective y movimiento libre
- **Scripts** - Sistema extensible de Scripts para lógica de comportamiento
- **Escalado Interno** - Renderizado a resolución fija con escala a ventana (letterbox/pillarbox)

## Requisitos

### Dependencias del Sistema

- **Linux (Fedora 44)**:
  ```bash
  sudo dnf install glfw-devel assimp-devel
  ```

- **Linux (Debian/Ubuntu)**:
  ```bash
  sudo apt install libglfw3-dev libassimp-dev
  ```

- **macOS** (con Homebrew):
  ```bash
  brew install glfw assimp
  ```

- **Windows** (MSYS2 UCRT64):
  ```bash
  pacman -S mingw-w64-ucrt-x86_64-glfw mingw-w64-ucrt-x86_64-assimp
  ```

### Herramientas de Desarrollo

- **Compilador**: GCC/G++ (C++17 o superior)
- **Build**: Make
- **IDE (opcional)**: CLion, VSCode

## Compilación y Ejecución

### Compilar

```bash
make
```

### Ejecutar

```bash
./main
```

### Limpiar

```bash
make clean
```

## Estructura del Proyecto

```
.
├── assets/                 # Contenido del juego
│   ├── models/            # Modelos 3D (OBJ, FBX, etc.)
│   ├── shaders/           # Archivos de shaders (*.vs, *.fs)
│   ├── textures/          # Texturas e imágenes
│   └── scripts/           # Scripts de comportamiento (C++)
├── include/               # Headers públicos
│   ├── Components/        # Componentes reutilizables
│   ├── Core/              # Sistema base (GameObject, Component, Input)
│   └── Render/            # Renderizado (Shader, Model, Mesh)
├── src/                   # Implementación
│   ├── Core/              # Implementación de Core
│   └── Render/            # Implementación de Render
├── vendor/                # Dependencias externas
│   ├── glad/              # OpenGL loader
│   ├── GLFW/              # Ventana y entrada
│   └── glm/               # Matemáticas vectoriales
├── main.cpp               # Punto de entrada
├── Makefile               # Script de compilación
└── README.md              # Este archivo
```

## Componentes Disponibles

La arquitectura está basada en GameObjects que contienen Componentes:

### Core Components

- **TransformComponent3D** - Posición, rotación (Euler) y escala en 3D
- **CameraComponent** - Cámara con proyección perspective
- **InputComponent** - Manejo de entrada de teclado y ratón
- **MeshComponent** - Renderización de modelos 3D

### Componentes Adicionales

- **RigidBodyComponent3D** - Física (cuerpo rígido)
- **ColliderComponent3D** - Colisiones 3D
- **SoundComponent3D** - Audio espacial 3D
- **TerrainComponent3D** - Terreno procedural

### System Scripts

- **CameraController** - Movimiento de cámara con WASD y mouse look
- **SpinModel** - Rotación automática en el eje Y

## Uso Básico

### Crear un GameObject con Componentes

```cpp
auto myObject = std::make_unique<GameObject>();
myObject->Name = "MyActor";

// Añadir componentes
myObject->addComponent<TransformComponent>()->Position = {0, 0, 0};
myObject->addComponent<MeshComponent>("assets/models/mymodel.obj");
myObject->addComponent<MyScript>();  // Script personalizado

// Inicializar
myObject->start();
```

### Crear un Script Personalizado

Hereda de `Script` e implementa `start()` y `tick(dt)`:

```cpp
class MyScript : public Script {
    TransformComponent* tf;
    
public:
    void start() override {
        tf = owner->getComponent<TransformComponent>();
    }
    
    void tick(float dt) override {
        // Lógica del juego aquí
        tf->Position.y += 1.0f * dt;
    }
};
```

### Crear un Shader Personalizado

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
    FragColor = vec4(1.0, 0.0, 0.0, 1.0);  // Rojo
}
```

## Control en Tiempo de Ejecución

| Tecla | Acción |
|-------|--------|
| W | Mover adelante |
| S | Mover atrás |
| A | Mover izquierda |
| D | Mover derecha |
| Q | Mover arriba |
| E | Mover abajo |
| Ratón | Rotar vista |
| ESC | Salir |

## Configuración de Resolución

En `main.cpp`:

```cpp
// Resolución de ventana
unsigned int SCR_WIDTH = 1920;
unsigned int SCR_HEIGHT = 1080;

// Resolución interna de renderizado (independiente de ventana)
const int RENDER_WIDTH  = 1920;
const int RENDER_HEIGHT = 1080;
```

## Detalles Técnicos

### Renderizado Offscreen

El proyecto utiliza Framebuffer Objects (FBO) para:
- Renderizar a una resolución interna fija
- Escalar a la resolución de ventana sin afectar rendimiento
- Mantener aspect ratio constante (letterbox/pillarbox)

### Sistema de Entrada

El `InputManager` mapea acciones a teclas:

```cpp
InputManager input(window);
input.BindAction("Jump", GLFW_KEY_SPACE);
input.IsActionPressed("Jump");
```

### Sistema de Componentes

Basado en templates C++ para tipos estáticos:

```cpp
// Obtener componente (nullptr si no existe)
auto* camera = gameObject->getComponent<CameraComponent>();

// Añadir componente
auto* mesh = gameObject->addComponent<MeshComponent>("path/to/model.obj");
```

## Troubleshooting

### "Failed to initialize GLAD"
Asegúrate que OpenGL 3.3 o superior está disponible en tu GPU.

### "Texture failed to load"
Verifica que las rutas de texturas sean relativas al directorio de ejecución.

### Errores de compilación con includes
Ejecuta `make clean` y vuelve a compilar.

## Licencia

Este proyecto está disponible bajo la licencia MIT.

## Notas de Desarrollo

- El sistema de componentes usa `std::unique_ptr` para gestión automática de memoria
- Los Scripts se ejecutan en orden de adición
- TransformComponent3D usa ángulos de Euler (grados) para rotación
- Todos los shaders deben estar en formato GLSL 3.30

## Siguiente Pasos

- [ ] Implementar sistema de física más completo
- [ ] Añadir GUI/HUD
- [ ] Optimizar culling de geometría
- [ ] Sistema de partículas
- [ ] Iluminación dinámica

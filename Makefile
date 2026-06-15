# === Flags ===
# OpenMP: enables the #pragma omp parallel collision sweep in
# include/Components/Physics/ColliderComponent3D.h. Needed on both the compile of
# main.o and the final link (to pull in the libgomp runtime).
OPENMP = -fopenmp

# Include search roots. With these, sources include headers by stable logical
# path (e.g. "Core/...", <glm/glm.hpp>) instead of brittle ../../ chains, so moving
# a file only changes a -I root here, not every relative include.
#   -I.                     -> project root (resolves "assets/scripts/..." )
#   -Iinclude               -> engine headers: "Core/...", "Render/...", "Components/..."
#   -isystem vendor/glad    -> <glad/glad.h>, <KHR/khrplatform.h>
#   -isystem vendor/glm     -> <glm/glm.hpp>, <glm/gtc/...>
#   -isystem vendor/GLFW... -> <glfw3.h>
# (-isystem also silences warnings originating in third-party headers.)
INCLUDES = -I. -Iinclude -isystem vendor/glad -isystem vendor/glm -isystem vendor/GLFW/include -isystem vendor/json

# Extra include root for the glTF loader TU only: lets tiny_gltf.h find itself + its bundled json.hpp.
# Kept off the shared INCLUDES so its json.hpp / stb_image.h can't shadow the engine's elsewhere.
TINYGLTF_INC = -isystem vendor/tinyglTF/tinygltf-release

# === Output layout ===
# Objects build into build/lib, the executable into build/. Change NAME to rename the
# binary (the .exe suffix is added automatically on Windows).
NAME   = game
BUILD  = build
OBJDIR = $(BUILD)/lib

# === Source files ===
GLAD          = vendor/glad/glad.c
SHADER        = src/Render/shader_s.cpp
ASSET_LOADER  = src/Core/lib_asset_loader.cpp
INPUT_MANAGER = src/Core/input_manager.cpp
GL_RENDERER   = src/Render/OpenGLRenderer.cpp
GO_LOADER     = src/Core/GameObjectLoader.cpp
GLTF_LOADER   = src/Core/gltf_loader.cpp

# === Platform-specific settings ===
ifeq ($(OS),Windows_NT)
    # Windows (MinGW / MSYS2 UCRT64, invoked from cmd via mingw32-make)
    #   pacman -S mingw-w64-ucrt-x86_64-glfw mingw-w64-ucrt-x86_64-assimp
    LIBS  = -lglfw3 -lassimp -lgdi32 -lopengl32
    EXEC  = $(BUILD)/$(NAME).exe
    MKDIR = if not exist "$(subst /,\,$(OBJDIR))" mkdir "$(subst /,\,$(OBJDIR))"
    CLEAN = if exist "$(subst /,\,$(BUILD))" rmdir /s /q "$(subst /,\,$(BUILD))"
else
    # Linux
    #   apt install libglfw3-dev libassimp-dev
    LIBS  = -lglfw -lassimp -lGL -ldl -lpthread
    EXEC  = $(BUILD)/$(NAME)
    MKDIR = mkdir -p $(OBJDIR)
    CLEAN = rm -rf $(BUILD)
endif

# === Objects (all under $(OBJDIR)) ===
OBJS = $(OBJDIR)/main.o $(OBJDIR)/glad.o $(OBJDIR)/shader_s.o $(OBJDIR)/lib_asset_loader.o \
       $(OBJDIR)/input_manager.o $(OBJDIR)/OpenGLRenderer.o $(OBJDIR)/GameObjectLoader.o \
       $(OBJDIR)/gltf_loader.o

# === Build rules ===
.PHONY: all recompile clean

all: $(EXEC)

$(EXEC): $(OBJS)
	g++ $(OPENMP) -o $(EXEC) $(OBJS) $(LIBS)

# Force-rebuild only main.o (it transitively includes all header-only scripts and
# components) and relink. Use this when you've only edited files under assets/scripts/
# or include/ — the other objects are reused.
recompile: $(filter-out $(OBJDIR)/main.o,$(OBJS)) | $(OBJDIR)
	g++ $(OPENMP) $(INCLUDES) -c main.cpp -o $(OBJDIR)/main.o
	g++ $(OPENMP) -o $(EXEC) $(OBJS) $(LIBS)

$(OBJDIR)/main.o: main.cpp | $(OBJDIR)
	g++ $(OPENMP) $(INCLUDES) -c $< -o $@

$(OBJDIR)/OpenGLRenderer.o: $(GL_RENDERER) | $(OBJDIR)
	g++ $(OPENMP) $(INCLUDES) -c $< -o $@

$(OBJDIR)/GameObjectLoader.o: $(GO_LOADER) | $(OBJDIR)
	g++ $(OPENMP) $(INCLUDES) -c $< -o $@

# The glTF loader gets the extra tinygltf include root; tiny_gltf.h's implementation is heavy so this
# object is large/slow to compile, but it only rebuilds when gltf_loader.cpp itself changes.
$(OBJDIR)/gltf_loader.o: $(GLTF_LOADER) | $(OBJDIR)
	g++ $(INCLUDES) $(TINYGLTF_INC) -c $< -o $@

$(OBJDIR)/glad.o: $(GLAD) | $(OBJDIR)
	gcc $(INCLUDES) -c $< -o $@

$(OBJDIR)/shader_s.o: $(SHADER) | $(OBJDIR)
	g++ $(INCLUDES) -c $< -o $@

$(OBJDIR)/lib_asset_loader.o: $(ASSET_LOADER) | $(OBJDIR)
	g++ $(INCLUDES) -c $< -o $@

$(OBJDIR)/input_manager.o: $(INPUT_MANAGER) | $(OBJDIR)
	g++ $(INCLUDES) -c $< -o $@

# Order-only prerequisite: create the output dir once, without forcing relinks when
# its timestamp changes.
$(OBJDIR):
	$(MKDIR)

clean:
	$(CLEAN)

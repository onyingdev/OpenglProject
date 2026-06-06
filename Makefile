# === Flags ===
# OpenMP: enables the #pragma omp parallel collision sweep in
# include/Components/ColliderComponent3D.h. Needed on both the compile of
# main.o and the final link (to pull in the libgomp runtime).
OPENMP = -fopenmp

# Include search roots. With these, sources include headers by stable logical
# path (e.g. "Core/Component.h", <glm/glm.hpp>) instead of brittle ../../ chains,
# so moving a file only changes a -I root here, not every relative include.
#   -I.                     -> project root (resolves "assets/scripts/..." in main.cpp)
#   -Iinclude               -> engine headers: "Core/...", "Render/...", "Components/..."
#   -isystem vendor/glad    -> <glad/glad.h>, <KHR/khrplatform.h>
#   -isystem vendor/glm     -> <glm/glm.hpp>, <glm/gtc/...>
#   -isystem vendor/GLFW... -> <glfw3.h>
# (-isystem also silences warnings originating in third-party headers.)
INCLUDES = -I. -Iinclude -isystem vendor/glad -isystem vendor/glm -isystem vendor/GLFW/include

# === Source files ===
GLAD          = vendor/glad/glad.c
SHADER        = src/Render/shader_s.cpp
ASSET_LOADER  = src/Core/lib_asset_loader.cpp
INPUT_MANAGER = src/Core/input_manager.cpp

# === Platform-specific settings ===
ifeq ($(OS),Windows_NT)
    # Windows (MinGW / MSYS2 UCRT64)
    # Install dependencies via:
    #   pacman -S mingw-w64-ucrt-x86_64-glfw mingw-w64-ucrt-x86_64-assimp
    # GLFW on Windows is named libglfw3 (note the "3" suffix).
    LIBS = -lglfw3 -lassimp -lgdi32 -lopengl32
    RM   = del /q /f
    EXEC = main.exe
else
    # Linux
    # Install dependencies via:
    #   apt install libglfw3-dev libassimp-dev
    LIBS = -lglfw -lassimp -lGL -ldl -lpthread
    RM   = rm -f
    EXEC = main
endif

# === Build rules ===
.PHONY: all clean scripts

all: main.o glad.o shader_s.o lib_asset_loader.o input_manager.o
	g++ $(OPENMP) -o $(EXEC) main.o glad.o shader_s.o lib_asset_loader.o input_manager.o $(LIBS)

# Force-rebuild only main.o (which transitively includes all header-only scripts
# and components) and relink. Use this when you've only edited files under
# assets/scripts/ or include/ — skips recompiling glad/shader/asset_loader/input_manager.
# Their .o files are listed as deps so a fresh checkout still works (they'll be
# built once and then reused on subsequent `make scripts` runs).
recompile: glad.o shader_s.o lib_asset_loader.o input_manager.o
	g++ $(OPENMP) $(INCLUDES) -c main.cpp
	g++ $(OPENMP) -o $(EXEC) main.o glad.o shader_s.o lib_asset_loader.o input_manager.o $(LIBS)

main.o: main.cpp
	g++ $(OPENMP) $(INCLUDES) -c main.cpp

glad.o: $(GLAD)
	gcc $(INCLUDES) -c $(GLAD)

shader_s.o: $(SHADER)
	g++ $(INCLUDES) -c $(SHADER)

lib_asset_loader.o: $(ASSET_LOADER)
	g++ $(INCLUDES) -c $(ASSET_LOADER)

input_manager.o: $(INPUT_MANAGER)
	g++ $(INCLUDES) -c $(INPUT_MANAGER)

clean:
	$(RM) *.o $(EXEC)

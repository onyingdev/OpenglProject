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
	g++ -o $(EXEC) main.o glad.o shader_s.o lib_asset_loader.o input_manager.o $(LIBS)

# Force-rebuild only main.o (which transitively includes all header-only scripts
# and components) and relink. Use this when you've only edited files under
# assets/scripts/ or include/ — skips recompiling glad/shader/asset_loader/input_manager.
# Their .o files are listed as deps so a fresh checkout still works (they'll be
# built once and then reused on subsequent `make scripts` runs).
recompile: glad.o shader_s.o lib_asset_loader.o input_manager.o
	g++ -c main.cpp
	g++ -o $(EXEC) main.o glad.o shader_s.o lib_asset_loader.o input_manager.o $(LIBS)

main.o: main.cpp
	g++ -c main.cpp

glad.o: $(GLAD)
	gcc -c $(GLAD)

shader_s.o: $(SHADER)
	g++ -c $(SHADER)

lib_asset_loader.o: $(ASSET_LOADER)
	g++ -c $(ASSET_LOADER)

input_manager.o: $(INPUT_MANAGER)
	g++ -c $(INPUT_MANAGER)

clean:
	$(RM) *.o $(EXEC)

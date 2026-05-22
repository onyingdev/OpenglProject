LIB_PATH = vendor/GLFW/lib
LIB_GL = libglfw3.a
GLAD = vendor/glad/glad.c
SHADER = assets/shaders/shader_s.cpp
ASSET_LOADER = src/Core/lib_asset_loader.cpp
INPUT_MANAGER = src/Core/input_manager.cpp

# WINDOWS LINKING FLAGS
# You must install GLFW on windows
LIBS = -lglfw3 -lgdi32 -lopengl32

ifeq ($(OS),Windows_NT)
    # Windows
    RM = del /q /f
    EXEC = main.exe
else
    # Linux / Mac
    RM = rm -f
    EXEC = main
endif

#windows: main.o glad.o shader_s.o lib_asset_loader.o
#	g++ -o main main.o glad.o shader_s.o lib_asset_loader.o $(LIBS)

make: main.o glad.o shader_s.o lib_asset_loader.o input_manager.o
	g++ -o main main.o glad.o shader_s.o lib_asset_loader.o input_manager.o -L$(LIB_PATH) -l:$(LIB_GL) -lassimp

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
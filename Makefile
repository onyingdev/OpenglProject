LIB_PATH = vendor/GLFW/lib
LIB_GL = libglfw3.a
GLAD = vendor/glad/glad.c
SHADER = assets/shaders/shader_s.cpp
ASSET_LOADER = src/Core/lib_asset_loader.cpp

make: main.o glad.o shader_s.o lib_asset_loader.o
	g++ -o main main.o glad.o shader_s.o lib_asset_loader.o -L$(LIB_PATH) -l:$(LIB_GL)

main.o: main.cpp
	g++ -c main.cpp

glad.o: $(GLAD)
	gcc -c $(GLAD)

shader_s.o: $(SHADER)
	g++ -c $(SHADER)

lib_asset_loader.o: $(ASSET_LOADER)
	g++ -c $(ASSET_LOADER)

clean:
	rm -f *.o main
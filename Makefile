LIB_PATH = vendor/GLFW/lib
LIB_GL = libglfw3.a
GLAD = vendor/glad/glad.c
SHADER = assets/shaders/shader_s.cpp

make: main.o glad.o shader_s.o
	g++ -o main main.o glad.o shader_s.o -L$(LIB_PATH) -l:$(LIB_GL)

main.o: main.cpp
	g++ -c main.cpp

glad.o: $(GLAD)
	gcc -c $(GLAD)

shader_s.o: $(SHADER)
	g++ -c $(SHADER)

clean:
	rm -f *.o main
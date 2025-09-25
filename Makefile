LIB_PATH = GLFW/lib
LIB_GL = libglfw3.a
GLAD = glad/glad.c

make: main.o glad.o
	g++ -o main main.o glad.o -L$(LIB_PATH) -l:$(LIB_GL)

main.o: main.cpp
	g++ -c main.cpp

glad.o: $(GLAD)
	gcc -c $(GLAD)

clean:
	rm -f *.o
all: 3D-floating-shape

3D-floating-shape: 3D-floating-shape.cpp
	g++ -std=c++17 -Wall -Wextra -O3 -o 3D-floating-shape 3D-floating-shape.cpp -lX11 -lm

clean:
	rm -f 3D-floating-shape

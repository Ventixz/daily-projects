CXX = g++
CXXFLAGS = -std=c++17 -Wall -Wextra -Wpedantic -O2 -Iinclude
BIN = bin

.PHONY: all test run clean

all: $(BIN)/raytracer

$(BIN):
	mkdir -p $(BIN) renders

$(BIN)/raytracer: src/main.cpp src/render.cpp include/render.hpp include/scene.hpp include/geometry.hpp | $(BIN)
	$(CXX) $(CXXFLAGS) src/main.cpp src/render.cpp -o $(BIN)/raytracer

$(BIN)/test_raytracer: tests/test_raytracer.cpp src/render.cpp include/render.hpp include/scene.hpp include/geometry.hpp | $(BIN)
	$(CXX) $(CXXFLAGS) tests/test_raytracer.cpp src/render.cpp -o $(BIN)/test_raytracer

test: $(BIN)/test_raytracer
	./$(BIN)/test_raytracer

# WIDTH=1024 HEIGHT=768 OUT=renders/out.ppm
run: $(BIN)/raytracer
	./$(BIN)/raytracer $(WIDTH) $(HEIGHT) $(OUT)

clean:
	rm -rf $(BIN)

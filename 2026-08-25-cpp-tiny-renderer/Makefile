CXX = g++
CXXFLAGS = -std=c++17 -Wall -Wextra -Wpedantic -O2 -Iinclude
BIN = bin

.PHONY: all test run clean

all: $(BIN)/tiny_renderer

$(BIN):
	mkdir -p $(BIN) renders

$(BIN)/tiny_renderer: src/main.cpp src/model.cpp src/render.cpp src/image.cpp include/model.hpp include/render.hpp include/image.hpp include/geometry.hpp | $(BIN)
	$(CXX) $(CXXFLAGS) src/main.cpp src/model.cpp src/render.cpp src/image.cpp -o $(BIN)/tiny_renderer

$(BIN)/test_tiny_renderer: tests/test_render.cpp src/model.cpp src/render.cpp src/image.cpp include/model.hpp include/render.hpp include/image.hpp include/geometry.hpp | $(BIN)
	$(CXX) $(CXXFLAGS) tests/test_render.cpp src/model.cpp src/render.cpp src/image.cpp -o $(BIN)/test_tiny_renderer

test: $(BIN)/test_tiny_renderer
	./$(BIN)/test_tiny_renderer

run: $(BIN)/tiny_renderer
	./$(BIN)/tiny_renderer

clean:
	rm -rf $(BIN)

CXX = g++
CXXFLAGS = -std=c++17 -Wall -Wextra -Wpedantic -O3 -mavx2 -mfma -Iinclude
BIN = bin

SRC = src/matmul_naive.cpp src/matmul_ikj.cpp src/matmul_simd.cpp src/matmul_kernel.cpp src/matmul_tiled.cpp
HDR = include/matmul.hpp src/micro_kernel.hpp

.PHONY: all test run clean

all: $(BIN)/matmul_bench

$(BIN):
	mkdir -p $(BIN)

$(BIN)/matmul_bench: src/main.cpp $(SRC) $(HDR) | $(BIN)
	$(CXX) $(CXXFLAGS) src/main.cpp $(SRC) -o $(BIN)/matmul_bench

$(BIN)/test_matmul: tests/test_matmul.cpp $(SRC) $(HDR) | $(BIN)
	$(CXX) $(CXXFLAGS) tests/test_matmul.cpp $(SRC) -o $(BIN)/test_matmul

test: $(BIN)/test_matmul
	./$(BIN)/test_matmul

run: $(BIN)/matmul_bench
	./$(BIN)/matmul_bench

clean:
	rm -rf $(BIN)

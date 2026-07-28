CXX = g++
CXXFLAGS = -std=c++17 -Wall -Wextra -Wpedantic -O2 -Iinclude
BIN = bin

.PHONY: all test run clean

all: $(BIN)/chip8

$(BIN):
	mkdir -p $(BIN)

$(BIN)/chip8: src/main.cpp src/chip8.cpp include/chip8.hpp | $(BIN)
	$(CXX) $(CXXFLAGS) src/main.cpp src/chip8.cpp -o $(BIN)/chip8

$(BIN)/test_chip8: tests/test_chip8.cpp src/chip8.cpp include/chip8.hpp | $(BIN)
	$(CXX) $(CXXFLAGS) tests/test_chip8.cpp src/chip8.cpp -o $(BIN)/test_chip8

test: $(BIN)/test_chip8
	./$(BIN)/test_chip8

# ROM=roms/whatever.ch8
run: $(BIN)/chip8
	./$(BIN)/chip8 $(ROM)

clean:
	rm -rf $(BIN)

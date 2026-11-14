CXX      ?= g++
CC       ?= gcc
CXXFLAGS ?= -std=c++17 -O2 -Wall -Wextra -Wpedantic
CPPFLAGS += -Iinclude

LIB_SRC := src/debugger.cpp src/breakpoint.cpp src/registers.cpp src/elf_symbols.cpp
LIB_OBJ := $(LIB_SRC:src/%.cpp=build/%.o)

all: build/minidbg build/target build/target-nopie

build/%.o: src/%.cpp $(wildcard include/*.hpp) | build
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -c $< -o $@

build/minidbg: $(LIB_OBJ) build/main.o
	$(CXX) $(CXXFLAGS) $^ -o $@

# -O0 keeps add() a real call; one PIE and one non-PIE build exercise both address paths.
build/target: tests/target.c | build
	$(CC) -O0 -fPIE -pie $< -o $@
build/target-nopie: tests/target.c | build
	$(CC) -O0 -fno-pie -no-pie $< -o $@
build/crash: tests/crash.c | build
	$(CC) -O0 $< -o $@

build/test_debugger: tests/test_debugger.cpp $(LIB_OBJ) | build
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) $^ -o $@

test: build/test_debugger build/target build/target-nopie build/crash
	./build/test_debugger

build:
	mkdir -p build

clean:
	rm -rf build

.PHONY: all test clean

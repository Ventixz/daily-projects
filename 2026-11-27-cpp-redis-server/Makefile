CXX      ?= g++
CXXFLAGS ?= -std=c++17 -O2 -g -Wall -Wextra -Wpedantic -pthread
SANITIZE  = -fsanitize=address,undefined -fno-omit-frame-pointer

LIB = src/hashtable.cpp src/avl.cpp src/zset.cpp src/heap.cpp \
      src/thread_pool.cpp src/protocol.cpp src/store.cpp
HDR = $(wildcard src/*.h) test/check.h

.PHONY: all test unit integration run clean

all: build/server build/client

build/server: src/server.cpp $(LIB) $(HDR)
	@mkdir -p build
	$(CXX) $(CXXFLAGS) -o $@ src/server.cpp $(LIB)

build/client: src/client.cpp src/protocol.cpp $(HDR)
	@mkdir -p build
	$(CXX) $(CXXFLAGS) -o $@ src/client.cpp src/protocol.cpp

# Tests run against sanitizer builds, so memory errors and UB fail the suite.
build/san/server: src/server.cpp $(LIB) $(HDR)
	@mkdir -p build/san
	$(CXX) $(CXXFLAGS) $(SANITIZE) -o $@ src/server.cpp $(LIB)

build/san/unit_tests: test/unit_tests.cpp $(LIB) $(HDR)
	@mkdir -p build/san
	$(CXX) $(CXXFLAGS) $(SANITIZE) -Isrc -o $@ test/unit_tests.cpp $(LIB)

build/san/integration_tests: test/integration_tests.cpp src/protocol.cpp $(HDR)
	@mkdir -p build/san
	$(CXX) $(CXXFLAGS) $(SANITIZE) -Isrc -o $@ test/integration_tests.cpp src/protocol.cpp

unit: build/san/unit_tests
	./build/san/unit_tests

integration: build/san/integration_tests build/san/server
	./build/san/integration_tests ./build/san/server

test: unit integration

run: build/server
	./build/server

clean:
	rm -rf build

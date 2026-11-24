CXX      ?= g++
CXXFLAGS ?= -std=c++17 -O2 -g -Wall -Wextra -Werror
LDFLAGS  ?= -pthread

LIB_SRCS := src/hashtable.cpp src/avl.cpp src/zset.cpp src/heap.cpp src/resp.cpp src/store.cpp src/thread_pool.cpp
LIB_OBJS := $(LIB_SRCS:src/%.cpp=build/%.o)

all: build/redis-server build/unit-tests

build/%.o: src/%.cpp src/*.h | build
	$(CXX) $(CXXFLAGS) -c $< -o $@

build/redis-server: build/server.o $(LIB_OBJS)
	$(CXX) $(CXXFLAGS) $^ -o $@ $(LDFLAGS)

build/unit-tests: tests/unit_tests.cpp $(LIB_OBJS) src/*.h
	$(CXX) $(CXXFLAGS) -Isrc tests/unit_tests.cpp $(LIB_OBJS) -o $@ $(LDFLAGS)

build:
	mkdir -p build

# Unit tests, then end-to-end tests against the real binary (and a real
# Redis for differential checks, when redis-server is installed).
test: all
	./build/unit-tests
	python3 tests/e2e_test.py

# Same tests under AddressSanitizer + UndefinedBehaviorSanitizer.
sanitize:
	$(MAKE) clean
	$(MAKE) test CXXFLAGS="-std=c++17 -O1 -g -Wall -Wextra -Werror -fsanitize=address,undefined -fno-omit-frame-pointer" LDFLAGS="-pthread -fsanitize=address,undefined"

clean:
	rm -rf build

.PHONY: all test sanitize clean

CXX := g++
CXXFLAGS := -std=c++17 -O2 -Wall -Wextra -Iinclude
LDFLAGS := -ldl

$(shell mkdir -p build)

.PHONY: all build test e2e run clean

all: build

build: build/host build/plugin.so

build/host: src/host.cpp src/reloader.cpp src/reloader.hpp include/plugin_api.h
	$(CXX) $(CXXFLAGS) -o $@ src/host.cpp src/reloader.cpp $(LDFLAGS)

build/plugin.so: plugin/plugin.cpp include/plugin_api.h
	$(CXX) $(CXXFLAGS) -fPIC -shared -o $@ plugin/plugin.cpp

build/fixture_v1.so: tests/fixtures/fixture_v1.cpp include/plugin_api.h
	$(CXX) $(CXXFLAGS) -fPIC -shared -o $@ $<

build/fixture_v2.so: tests/fixtures/fixture_v2.cpp include/plugin_api.h
	$(CXX) $(CXXFLAGS) -fPIC -shared -o $@ $<

test: build/fixture_v1.so build/fixture_v2.so
	$(CXX) $(CXXFLAGS) -o build/reloader_test tests/unit/reloader_test.cpp src/reloader.cpp $(LDFLAGS)
	./build/reloader_test

e2e: build
	bash e2e/smoke.sh

run: build
	./build/host build/plugin.so 200 0.05 100

clean:
	rm -rf build

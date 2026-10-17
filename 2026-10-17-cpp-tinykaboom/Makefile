CXX ?= g++
CXXFLAGS ?= -std=c++17 -O2 -Wall -Wextra

all: kaboom

kaboom: main.cpp kaboom.cpp kaboom.h
	$(CXX) $(CXXFLAGS) main.cpp kaboom.cpp -o kaboom

test_kaboom: test_kaboom.cpp kaboom.cpp kaboom.h
	$(CXX) $(CXXFLAGS) test_kaboom.cpp kaboom.cpp -o test_kaboom

test: test_kaboom
	./test_kaboom

clean:
	rm -f kaboom test_kaboom kaboom.ppm

.PHONY: all test clean

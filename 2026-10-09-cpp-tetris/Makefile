CXXFLAGS ?= -std=c++17 -O2 -Wall -Wextra
all: tetris test
tetris: main.cpp tetris.hpp
	$(CXX) $(CXXFLAGS) main.cpp -o tetris
test: test.cpp tetris.hpp
	$(CXX) $(CXXFLAGS) test.cpp -o run_tests && ./run_tests
clean:
	rm -f tetris run_tests
.PHONY: all test clean

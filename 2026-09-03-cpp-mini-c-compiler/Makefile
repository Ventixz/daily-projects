CXX := g++
CXXFLAGS := -std=c++17 -Wall -Wextra -O2
BUILD := build
SRC := $(wildcard src/*.cpp)
HDR := $(wildcard src/*.h)

.PHONY: all test unit-test clean run

all: $(BUILD)/mcc

$(BUILD):
	mkdir -p $(BUILD)

$(BUILD)/mcc: $(SRC) $(HDR) | $(BUILD)
	$(CXX) $(CXXFLAGS) $(SRC) -o $@

$(BUILD)/test_lexer: tests/unit/test_lexer.cpp src/lexer.cpp src/lexer.h | $(BUILD)
	$(CXX) $(CXXFLAGS) -Isrc tests/unit/test_lexer.cpp src/lexer.cpp -o $@

$(BUILD)/test_parser: tests/unit/test_parser.cpp src/lexer.cpp src/parser.cpp src/resolver.cpp $(HDR) | $(BUILD)
	$(CXX) $(CXXFLAGS) -Isrc tests/unit/test_parser.cpp src/lexer.cpp src/parser.cpp src/resolver.cpp -o $@

unit-test: $(BUILD)/test_lexer $(BUILD)/test_parser
	$(BUILD)/test_lexer
	$(BUILD)/test_parser

# End-to-end: compiles every tests/programs/*.c with mcc and, separately, with
# gcc directly, then asserts the two executables agree on stdout and exit code.
# Also checks that every tests/invalid/*.c is rejected by mcc.
test: all unit-test
	python3 tests/run_tests.py

run: all
	./$(BUILD)/mcc examples/fib.c -o $(BUILD)/fib.s
	gcc $(BUILD)/fib.s runtime/runtime.c -o $(BUILD)/fib
	./$(BUILD)/fib

clean:
	rm -rf $(BUILD)

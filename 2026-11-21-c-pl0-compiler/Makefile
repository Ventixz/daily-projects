CC      ?= cc
CFLAGS  ?= -std=c11 -O2 -Wall -Wextra -Wpedantic -Wshadow
SRC      = src/lexer.c src/compiler.c

all: pl0c

pl0c: $(SRC) src/main.c src/lexer.h src/compiler.h
	$(CC) $(CFLAGS) -o $@ $(SRC) src/main.c

tests/unit_tests: $(SRC) tests/unit_tests.c src/lexer.h src/compiler.h
	$(CC) $(CFLAGS) -Isrc -o $@ $(SRC) tests/unit_tests.c

test: pl0c tests/unit_tests
	./tests/unit_tests
	CC="$(CC)" ./tests/run_tests.sh

clean:
	rm -f pl0c tests/unit_tests
	rm -rf build

.PHONY: all test clean

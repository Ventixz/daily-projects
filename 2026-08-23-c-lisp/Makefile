CC = gcc
CFLAGS = -std=c99 -D_DEFAULT_SOURCE -Wall -Wextra -Wpedantic -g
LDLIBS = -lm

SRC = src/lval.c src/lenv.c src/parser.c src/builtins.c

.PHONY: all test clean

all: lisp

lisp: src/main.c $(SRC)
	$(CC) $(CFLAGS) -o $@ $^ $(LDLIBS)

test: run_tests
	./run_tests

run_tests: tests/test_lisp.c $(SRC)
	$(CC) $(CFLAGS) -o $@ $^ $(LDLIBS)

clean:
	rm -f lisp run_tests

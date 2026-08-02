CC = gcc
CFLAGS = -std=c99 -D_DEFAULT_SOURCE -Wall -Wextra -Wpedantic -g
LDLIBS = -lpthread

SRC = src/allocator.c

.PHONY: all test clean

all: alloc_demo

alloc_demo: src/main.c $(SRC)
	$(CC) $(CFLAGS) -o $@ $^ $(LDLIBS)

test: run_tests
	./run_tests

run_tests: tests/test_allocator.c $(SRC)
	$(CC) $(CFLAGS) -o $@ $^ $(LDLIBS)

clean:
	rm -f alloc_demo run_tests

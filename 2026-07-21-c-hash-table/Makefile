CC = gcc
CFLAGS = -std=c99 -Wall -Wextra -Wpedantic -g
LDLIBS = -lm

SRC = src/hash_table.c src/prime.c

.PHONY: all test clean

all: hash_table_demo

hash_table_demo: src/main.c $(SRC)
	$(CC) $(CFLAGS) -o $@ $^ $(LDLIBS)

test: run_tests
	./run_tests

run_tests: tests/test_hash_table.c $(SRC)
	$(CC) $(CFLAGS) -o $@ $^ $(LDLIBS)

clean:
	rm -f hash_table_demo run_tests

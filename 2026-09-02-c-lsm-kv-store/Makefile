CC = gcc
CFLAGS = -std=c99 -D_DEFAULT_SOURCE -Wall -Wextra -Wpedantic -g

SRC = src/memtable.c src/record.c src/wal.c src/sstable.c src/store.c

.PHONY: all test clean

all: kv_demo

kv_demo: src/main.c $(SRC)
	$(CC) $(CFLAGS) -o $@ $^

test: run_tests
	./run_tests

run_tests: tests/test_kv.c $(SRC)
	$(CC) $(CFLAGS) -o $@ $^

clean:
	rm -f kv_demo run_tests
	rm -rf kvdata test_kvdata*

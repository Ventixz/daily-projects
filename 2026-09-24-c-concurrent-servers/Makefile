CC = gcc
CFLAGS = -std=c11 -D_DEFAULT_SOURCE -Wall -Wextra -Wpedantic -g
LDFLAGS_THREADED = -lpthread

COMMON = src/common.c

.PHONY: all test test-c test-py clean

all: sequential threaded epoll

sequential: src/server_sequential.c $(COMMON)
	$(CC) $(CFLAGS) -o $@ $^

threaded: src/server_threaded.c $(COMMON)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS_THREADED)

epoll: src/server_epoll.c $(COMMON)
	$(CC) $(CFLAGS) -o $@ $^

run_unit_tests: tests/test_common.c $(COMMON)
	$(CC) $(CFLAGS) -o $@ $^

test-c: run_unit_tests
	./run_unit_tests

test-py: all
	python3 tests/test_correctness.py
	python3 tests/test_concurrency.py

test: test-c test-py

clean:
	rm -f sequential threaded epoll run_unit_tests

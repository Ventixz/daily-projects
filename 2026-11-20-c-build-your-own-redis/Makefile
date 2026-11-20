# make          build bin/redis-server and bin/redis-cli
# make test     build everything with ASan + UBSan and run all tests
# make clean

CC      ?= cc
CFLAGS  ?= -O2
WARN    := -std=c11 -D_POSIX_C_SOURCE=200809L -Wall -Wextra -Wpedantic -Wshadow -Werror
SAN     := -g -O1 -fsanitize=address,undefined -fno-omit-frame-pointer -fno-sanitize-recover=all

LIB_SRC := src/buf.c src/hashtable.c src/avl.c src/zset.c src/heap.c src/proto.c src/db.c
HDRS    := $(wildcard src/*.h) tests/test.h

all: bin/redis-server bin/redis-cli

bin/redis-server: $(LIB_SRC) src/server.c $(HDRS)
	@mkdir -p bin
	$(CC) $(WARN) $(CFLAGS) -o $@ $(LIB_SRC) src/server.c -lm

bin/redis-cli: src/buf.c src/proto.c src/client.c $(HDRS)
	@mkdir -p bin
	$(CC) $(WARN) $(CFLAGS) -o $@ src/buf.c src/proto.c src/client.c

build/test_ds: $(LIB_SRC) tests/test_ds.c $(HDRS)
	@mkdir -p build
	$(CC) $(WARN) $(SAN) -o $@ $(LIB_SRC) tests/test_ds.c -lm

build/test_db: $(LIB_SRC) tests/test_db.c $(HDRS)
	@mkdir -p build
	$(CC) $(WARN) $(SAN) -o $@ $(LIB_SRC) tests/test_db.c -lm

build/test_e2e: src/buf.c src/proto.c tests/test_e2e.c $(HDRS)
	@mkdir -p build
	$(CC) $(WARN) $(SAN) -o $@ src/buf.c src/proto.c tests/test_e2e.c

build/redis-server-san: $(LIB_SRC) src/server.c $(HDRS)
	@mkdir -p build
	$(CC) $(WARN) $(SAN) -o $@ $(LIB_SRC) src/server.c -lm

test: build/test_ds build/test_db build/test_e2e build/redis-server-san
	./build/test_ds
	./build/test_db
	./build/test_e2e ./build/redis-server-san

clean:
	rm -rf bin build

.PHONY: all test clean

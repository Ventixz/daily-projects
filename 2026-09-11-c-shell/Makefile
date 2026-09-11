CC = gcc
CFLAGS = -Wall -Wextra -std=c11 -D_DEFAULT_SOURCE -g
SRC = src/shell.c src/tokenizer.c src/parser.c src/executor.c src/builtins.c
BIN = shell

.PHONY: all clean test

all: $(BIN)

$(BIN): $(SRC) src/shell.h
	$(CC) $(CFLAGS) -o $(BIN) $(SRC)

test: $(BIN)
	bash tests/run_tests.sh

clean:
	rm -f $(BIN)

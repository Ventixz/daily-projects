CC = cc
CFLAGS = -Wall -Wextra -std=c11 -g
SRC = src/main.c src/parser.c src/builtins.c src/exec.c
BIN = myshell

$(BIN): $(SRC) src/shell.h
	$(CC) $(CFLAGS) -o $(BIN) $(SRC)

.PHONY: clean test
clean:
	rm -f $(BIN)

test: $(BIN)
	./tests/run_tests.sh

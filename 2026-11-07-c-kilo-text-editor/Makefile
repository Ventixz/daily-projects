CFLAGS = -std=gnu11 -Wall -Wextra -g -fsanitize=address,undefined
all: kilo test_editor
kilo: main.c editor.c editor.h
	$(CC) $(CFLAGS) -o $@ main.c editor.c
test_editor: test_editor.c editor.c editor.h
	$(CC) $(CFLAGS) -o $@ test_editor.c editor.c
test: test_editor
	./test_editor
clean:
	rm -f kilo test_editor

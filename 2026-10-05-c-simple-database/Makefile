CFLAGS ?= -std=gnu11 -Wall -Wextra -O2 -g

db: db.c
	$(CC) $(CFLAGS) -o $@ db.c

test: db
	sh tests/run.sh

clean:
	rm -f db *.db

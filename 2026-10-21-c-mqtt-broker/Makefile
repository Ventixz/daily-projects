CC ?= cc
CFLAGS ?= -std=gnu11 -O2 -Wall -Wextra -Werror

all: broker test_mqtt

broker: broker.c mqtt.c mqtt.h
	$(CC) $(CFLAGS) -o $@ broker.c mqtt.c

test_mqtt: test_mqtt.c mqtt.c mqtt.h
	$(CC) $(CFLAGS) -o $@ test_mqtt.c mqtt.c

test: all
	./test_mqtt
	python3 e2e_test.py ./broker

clean:
	rm -f broker test_mqtt

.PHONY: all test clean

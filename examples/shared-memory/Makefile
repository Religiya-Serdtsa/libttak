CC = gcc
CFLAGS = -Wall -I../../include -pthread
LDFLAGS = -L../../lib -lttak -pthread -lm

all: shared_example

shared_example: shared_example.c
	$(CC) $(CFLAGS) $< -o $@ $(LDFLAGS)

clean:
	rm -f shared_example

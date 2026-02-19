CC ?= gcc
CFLAGS = -Wall -std=c17 -pthread -I../include -O3 -march=native -flto -D_GNU_SOURCE
LDFLAGS = -L../lib -lttak -lm -pthread -flto

TARGETS = ttak_bench

all: $(TARGETS)

ttak_bench: ttak_bench.c
	$(CC) $(CFLAGS) $< -o $@ $(LDFLAGS)

clean:
	rm -f $(TARGETS)

.PHONY: all clean

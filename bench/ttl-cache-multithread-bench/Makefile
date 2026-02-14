CC ?= gcc
CFLAGS = -Wall -std=c11 -pthread -I../../include -O3 -march=native -flto -D_GNU_SOURCE
LDFLAGS = -L../../lib -lttak -lpthread -flto

TARGET = ttl_cache_bench
SRCS = ttl_cache_bench.c
OBJS = $(SRCS:.c=.o)

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) $(OBJS) -o $(TARGET) $(LDFLAGS)

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f $(TARGET) $(OBJS)

.PHONY: all clean

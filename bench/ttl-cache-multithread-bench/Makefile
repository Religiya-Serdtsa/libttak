CC = gcc
CFLAGS = -O0 -g -Wall -pthread -I../../include \
				 -fno-inline -fno-omit-frame-pointer \
				 -fno-optimize-sibling-calls \
				 -fno-jump-tables -fno-builtin \
				 -fno-strict-aliasing -fno-common \
				 -fno-stack-protector -fno-asynchronous-unwind-tables \
				 -fno-exceptions -fno-tree-vectorize -fno-strict-overflow \
				 -s -ffunction-sections -fdata-sections
LDFLAGS = -L../../lib -lttak -lpthread

TARGET = ttl_cache_bench
SRCS = ttl_cache_bench.c
OBJS = $(SRCS:.c=.o)

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) $(OBJS) -o $(TARGET) $(LDFLAGS)

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f $(TARGET) $(OBJS)

.PHONY: all clean

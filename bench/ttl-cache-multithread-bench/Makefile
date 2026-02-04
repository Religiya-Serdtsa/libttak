CC ?= tcc
CFLAGS = -Wall -std=c11 -pthread -Iinclude -O0 -g \
         -fno-inline -fno-omit-frame-pointer \
         -fno-optimize-sibling-calls \
         -fno-jump-tables -fno-builtin \
         -fno-strict-aliasing -fno-common \
         -fno-stack-protector -fno-asynchronous-unwind-tables \
         -fno-exceptions -fno-tree-vectorize -fno-strict-overflow \
         -ffunction-sections -fdata-sections
LDFLAGS = -lttak -lpthread

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

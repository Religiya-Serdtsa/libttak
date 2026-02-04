CC = tcc
LDFLAGS = -lttak -lpthread

TARGET = ttl_cache_bench
SRCS = ttl_cache_bench.c
OBJS = $(SRCS:.c=.o)

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) $(OBJS) -o $(TARGET) $(LDFLAGS)

%.o: %.c
	$(CC) -c $< -o $@

clean:
	rm -f $(TARGET) $(OBJS)

.PHONY: all clean

CC = tcc
CFLAGS = -Wall -std=c11 -pthread -Iinclude -O0 -g \
         -fno-inline -fno-omit-frame-pointer \
         -fno-optimize-sibling-calls \
         -fno-jump-tables -fno-builtin \
         -fno-strict-aliasing -fno-common \
         -fno-stack-protector -fno-asynchronous-unwind-tables \
         -fno-exceptions -fno-tree-vectorize -fno-strict-overflow \
         -ffunction-sections -fdata-sections
LDFLAGS = -lttak -lpthread -lm

TARGET = mersenne_explorer
SRCS = mersenne_explorer.c
OBJS = $(SRCS:.c=.o)

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) $(OBJS) -o $@ $(LDFLAGS)

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f $(OBJS) $(TARGET)

.PHONY: all clean

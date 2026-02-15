CC ?= gcc
CFLAGS ?= -Wall -Wextra -std=c11 -g -I../../include -D_GNU_SOURCE -D_XOPEN_SOURCE=700
LDFLAGS ?= -L../../lib -lttak -lpthread -lm
TARGET = lesson39_detachable_memory
SOURCE = lesson39_detachable_memory.c

.PHONY: all clean run

all: $(TARGET)

$(TARGET): $(SOURCE)
	$(CC) $(CFLAGS) $< -o $@ $(LDFLAGS)

run: $(TARGET)
	./$(TARGET)

clean:
	rm -f $(TARGET)

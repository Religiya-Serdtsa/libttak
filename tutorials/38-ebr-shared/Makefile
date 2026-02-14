CC ?= gcc
CFLAGS ?= -Wall -Wextra -std=c11 -g -I../../include -D_XOPEN_SOURCE=700
LDFLAGS ?= -lttak -lpthread -lm -lc
TARGET = lesson38_ebr_shared
SOURCE = lesson38_ebr_shared.c

.PHONY: all clean run

all: $(TARGET)

$(TARGET): $(SOURCE)
	$(CC) $(CFLAGS) $< -o $@ $(LDFLAGS)

run: $(TARGET)
	./$(TARGET)

clean:
	rm -f $(TARGET)

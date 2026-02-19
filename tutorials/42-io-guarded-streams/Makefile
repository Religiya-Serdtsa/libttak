CC ?= gcc
CFLAGS ?= -Wall -Wextra -std=c11 -g -I../../include -D_GNU_SOURCE -D_XOPEN_SOURCE=700
LDFLAGS ?= -L../../lib -lttak -lpthread -lm
TARGET = lesson42_io_guarded_streams
SOURCE = lesson42_io_guarded_streams.c

.PHONY: all clean run

all: $(TARGET)

$(TARGET): $(SOURCE)
	$(CC) $(CFLAGS) $< -o $@ $(LDFLAGS)

run: $(TARGET)
	./$(TARGET)

clean:
	rm -f $(TARGET)

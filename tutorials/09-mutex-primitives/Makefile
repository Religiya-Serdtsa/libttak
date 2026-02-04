
override CC = tcc
PREFIX ?= /usr/local
CFLAGS ?= -Wall -Wextra -pedantic -std=c11 -g
LIBTTAK_CFLAGS ?= -I$(PREFIX)/include
LIBTTAK_LDFLAGS ?= -L$(PREFIX)/lib -lttak -lpthread -lm
TARGET = lesson09_mutex_primitives
SOURCE = lesson09_mutex_primitives.c

.PHONY: all clean run

all: $(TARGET)

$(TARGET): $(SOURCE)
	$(CC) $(CFLAGS) $< -o $@ $(LIBTTAK_CFLAGS) $(LIBTTAK_LDFLAGS)

run: $(TARGET)
	./$(TARGET)

clean:
	rm -f $(TARGET)

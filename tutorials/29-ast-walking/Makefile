
override CC = tcc
PREFIX ?= /usr/local
CFLAGS ?= -Wall -Wextra -pedantic -std=c11 -g
LIBTTAK_CFLAGS ?= -I$(PREFIX)/include
LIBTTAK_LDFLAGS ?= -L$(PREFIX)/lib -lttak -lpthread -lm
TARGET = lesson29_ast_walking
SOURCE = lesson29_ast_walking.c

.PHONY: all clean run

all: $(TARGET)

$(TARGET): $(SOURCE)
	$(CC) $(CFLAGS) $< -o $@ $(LIBTTAK_CFLAGS) $(LIBTTAK_LDFLAGS)

run: $(TARGET)
	./$(TARGET)

clean:
	rm -f $(TARGET)

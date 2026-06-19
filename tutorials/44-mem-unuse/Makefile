
CC ?= tcc
CFLAGS ?= -Wall -Wextra -pedantic -std=c11 -g -D_GNU_SOURCE -D_XOPEN_SOURCE=700 -D_DEFAULT_SOURCE
LIBTTAK_CFLAGS ?=
LIBTTAK_LDFLAGS ?= -lttak -lpthread -lm
TARGET = lesson44_mem_unuse
SOURCE = lesson44_mem_unuse.c

.PHONY: all clean run

all: $(TARGET)

$(TARGET): $(SOURCE)
	$(CC) $(CFLAGS) $< -o $@ $(LIBTTAK_CFLAGS) $(LIBTTAK_LDFLAGS)

run: $(TARGET)
	./$(TARGET)

clean:
	rm -f $(TARGET)


CC ?= tcc
CFLAGS ?= -Wall -Wextra -pedantic -std=c11 -g -D_XOPEN_SOURCE=700
LIBTTAK_CFLAGS ?=
LIBTTAK_LDFLAGS ?= -lttak -lpthread -lm
TARGET = lesson07_set_container
SOURCE = lesson07_set_container.c

.PHONY: all clean run

all: $(TARGET)

$(TARGET): $(SOURCE)
	$(CC) $(CFLAGS) $< -o $@ $(LIBTTAK_CFLAGS) $(LIBTTAK_LDFLAGS)

run: $(TARGET)
	./$(TARGET)

clean:
	rm -f $(TARGET)

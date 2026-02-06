
CC ?= tcc
CFLAGS ?= -Wall -Wextra -pedantic -std=c11 -g -D_XOPEN_SOURCE=700
LIBTTAK_CFLAGS ?=
LIBTTAK_LDFLAGS ?= -lttak -lpthread -lm
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

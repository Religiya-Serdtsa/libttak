CC ?= tcc
CFLAGS ?= -std=c11 -Wall -Wextra -pedantic -I../../../include
LDFLAGS ?= -L../../../lib -lttak
TARGET ?= main

all: $(TARGET)

$(TARGET): main.c
	$(CC) $(CFLAGS) $< -o $@ $(LDFLAGS)

run: all
	./$(TARGET)

clean:
	rm -f $(TARGET)

.PHONY: all run clean

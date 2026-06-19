CC ?= tcc
CFLAGS ?= -std=c11 -Wall -Wextra -pedantic -D_GNU_SOURCE -D_XOPEN_SOURCE=700
LDFLAGS ?= -lttak
TARGET ?= helper

all: $(TARGET)

$(TARGET): main.c
	$(CC) $(CFLAGS) $< -o $@ $(LDFLAGS)

clean:
	rm -f $(TARGET)

.PHONY: all clean

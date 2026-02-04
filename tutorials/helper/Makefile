CC ?= tcc
CFLAGS ?= -std=c11 -Wall -Wextra -pedantic
LDFLAGS ?=
TARGET ?= helper

all: $(TARGET)

$(TARGET): main.c
	$(CC) $(CFLAGS) $< -o $@ $(LDFLAGS)

clean:
	rm -f $(TARGET)

.PHONY: all clean

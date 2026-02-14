CC ?= gcc
CFLAGS ?= -Wall -Wextra -std=c11 -g -I../../include -D_XOPEN_SOURCE=700
LDFLAGS ?= -L../../lib -lttak -lpthread -lm -lc
TARGET = ebr_demo
SOURCE = ebr_demo.c

.PHONY: all clean run

all: $(TARGET)

$(TARGET): $(SOURCE)
	$(CC) $(CFLAGS) $< -o $@ $(LDFLAGS)

run: $(TARGET)
	./$(TARGET)

clean:
	rm -f $(TARGET)

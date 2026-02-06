CC ?= gcc
CFLAGS ?= -Wall -Wextra -std=c11 -g -I../../include -D_XOPEN_SOURCE=700
LDFLAGS ?= -L../../lib -lttak -lpthread -lm -lc
TARGET = lesson36_trace_memory
SOURCE = lesson36_trace_memory.c

.PHONY: all clean run

all: $(TARGET)

$(TARGET): $(SOURCE)
	$(CC) $(CFLAGS) $< -o $@ $(LDFLAGS)

run: $(TARGET)
	./$(TARGET) 2> trace.log
	python3 ../../scripts/visualize_memory_calls.py trace.log

clean:
	rm -f $(TARGET)

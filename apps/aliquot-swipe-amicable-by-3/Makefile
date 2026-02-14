CC ?= tcc
# Build the aliquot swipe (amicable-by-3) against the in-tree libttak.
ROOT ?= ../..
LIBTTK ?= $(ROOT)/lib/libttak.a

CPPFLAGS += -I$(ROOT)/include
CFLAGS += -std=c11 -Wall
LDFLAGS += -L$(ROOT)/lib -lttak -pthread -lm

BIN_DIR = bin
SRC_DIR = src
TARGET = $(BIN_DIR)/aliquot_swipe

SRCS = $(SRC_DIR)/main.c
OBJS = $(SRCS:.c=.o)

all: $(TARGET)

$(TARGET): $(OBJS) $(LIBTTK)
	@mkdir -p $(BIN_DIR)
	$(CC) $(CFLAGS) $(OBJS) -o $@ $(LDFLAGS)

%.o: %.c
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@

$(LIBTTK):
	$(MAKE) -C $(ROOT) lib/libttak.a

clean:
	rm -f $(OBJS) $(TARGET)

.PHONY: all clean

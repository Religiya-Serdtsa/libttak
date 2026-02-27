CC ?= gcc
CFLAGS_GCC = -Wall -std=c11 -pthread -I../../include -O3 -march=native -flto -D_GNU_SOURCE
LDFLAGS_GCC = -L../../lib -lttak -lpthread -flto

CFLAGS_TCC = -Wall -std=c11 -pthread -I../../include -O3 -D_GNU_SOURCE
LDFLAGS_TCC = -L../../lib -lttak -lpthread

# Detect compiler
ifeq ($(notdir $(CC)),tcc)
	CFLAGS = $(CFLAGS_TCC)
	LDFLAGS = $(LDFLAGS_TCC)
else
	CFLAGS = $(CFLAGS_GCC)
	LDFLAGS = $(LDFLAGS_GCC)
endif

# Define all targets
TARGETS = ttl_cache_bench ttl_cache_bench_lockfree

# Source files for each target
SRCS_BENCH = ttl_cache_bench.c
SRCS_LOCKFREE = ttl_cache_bench_lockfree.c

# Object files
OBJS_BENCH = $(SRCS_BENCH:.c=.o)
OBJS_LOCKFREE = $(SRCS_LOCKFREE:.c=.o)

all: $(TARGETS)

# Target for the original benchmark
ttl_cache_bench: $(OBJS_BENCH)
	$(CC) $(CFLAGS) $(OBJS_BENCH) -o $@ $(LDFLAGS)

# Target for the lock-free benchmark
ttl_cache_bench_lockfree: $(OBJS_LOCKFREE)
	$(CC) $(CFLAGS) $(OBJS_LOCKFREE) -o $@ $(LDFLAGS)

# Pattern rule for object files
%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f $(TARGETS) $(OBJS_BENCH) $(OBJS_LOCKFREE)

.PHONY: all clean

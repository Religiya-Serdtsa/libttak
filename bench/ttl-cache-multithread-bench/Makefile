CC ?= gcc
CFLAGS_GCC = -Wall -std=c11 -pthread -I../../include -O3 -march=native -flto -D_GNU_SOURCE
LDFLAGS_GCC = -L../../lib -lttak -lpthread -flto

CFLAGS_TCC = -Wall -std=c11 -pthread -I../../include -O3 -D_GNU_SOURCE
LDFLAGS_TCC = -L../../lib -lttak -lpthread

TTAK_STATIC_LIB ?= ../../lib/libttak.a
TLS_SHIM_REQUIRED := 0

ifneq ($(wildcard $(TTAK_STATIC_LIB)),)
TLS_SHIM_REQUIRED := $(shell SHLIB="$(TTAK_STATIC_LIB)" sh -c 'if command -v nm >/dev/null 2>&1; then \
	if nm -g --defined-only "$$SHLIB" 2>/dev/null | grep -qw t_local_state; then \
		printf 0; \
	else \
		printf 1; \
	fi; \
else \
	printf 0; \
fi')
endif

# If CC=tcc is requested but tcc is unavailable, keep the lane alive using clang.
ifeq ($(notdir $(CC)),tcc)
TCC_BIN_OK := $(shell command -v tcc >/dev/null 2>&1 && echo 1 || echo 0)
ifeq ($(TCC_BIN_OK),0)
$(warning [bench] tcc not found; falling back to clang for CC=tcc bench lane)
override CC := clang
endif
endif

# Detect compiler
ifeq ($(notdir $(CC)),tcc)
	CFLAGS = $(CFLAGS_TCC)
	LDFLAGS = $(LDFLAGS_TCC)
else ifeq ($(findstring clang,$(notdir $(CC))),clang)
	CFLAGS = $(filter-out -flto,$(CFLAGS_GCC))
	LDFLAGS = $(filter-out -flto,$(LDFLAGS_GCC))
else
	CFLAGS = $(CFLAGS_GCC)
	LDFLAGS = $(LDFLAGS_GCC)
endif

ifeq ($(TLS_SHIM_REQUIRED),1)
CFLAGS += -DTTAK_EPOCH_FORCE_TLS_SHIM -DTTAK_MEM_FORCE_ACCESS_BRIDGE
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

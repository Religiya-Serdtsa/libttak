CC = gcc

# Standard flags for core modules: -O0 for maximum debuggability and predictable flow
CFLAGS = -Wall -pthread -MMD -MP -Iinclude -O0 -g \
         -fno-inline -fno-omit-frame-pointer \
         -fno-optimize-sibling-calls \
         -fno-jump-tables -fno-builtin \
         -fno-strict-aliasing -fno-common \
         -fno-stack-protector -fno-asynchronous-unwind-tables \
         -fno-exceptions -fno-tree-vectorize -fno-strict-overflow \
         -ffunction-sections -fdata-sections

# Optimization flags for math-heavy modules to handle intensive computations (e.g., Aliquot sequences)
MATH_CFLAGS = -O3 -march=native -mtune=native \
              -funroll-loops -ftree-vectorize -fstrict-aliasing \
              -finline-functions

LDFLAGS = -pthread -lm

PREFIX ?= /usr/local
LIBDIR = $(PREFIX)/lib
INCDIR = $(PREFIX)/include

SRC_DIRS = src/ht src/thread src/timing src/mem src/async src/priority \
           src/atomic src/sync src/math src/tree src/container \
           src/security src/mem_tree src/limit src/stats src/log

SRCS = $(foreach dir,$(SRC_DIRS),$(wildcard $(dir)/*.c))
OBJS = $(patsubst src/%.c,obj/%.o,$(SRCS))
DEPS = $(OBJS:.o=.d)

LIB = lib/libttak.a

TEST_SRCS = $(wildcard tests/test_*.c)
TEST_BINS = $(TEST_SRCS:.c=)

all: directories $(LIB)

$(LIB): $(OBJS)
	@mkdir -p lib
	ar rcs $@ $(OBJS)

# Specific rule for math directory: enables high optimization for performance-critical logic
obj/math/%.o: src/math/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $(MATH_CFLAGS) -c $< -o $@

# Default rule for all other directories
obj/%.o: src/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

test: all $(TEST_BINS)
	@echo "Starting internal test suite..."
	@for bin in $(TEST_BINS); do \
		echo "[RUNNING] $$bin"; \
		./$$bin; \
		if [ $$? -eq 0 ]; then \
			echo "[PASSED] $$bin"; \
		else \
			echo "[FAILED] $$bin with exit code $$?"; \
			exit 1; \
		fi \
	done
	@echo "All tests completed successfully."

tests/test_%: tests/test_%.c $(LIB)
	$(CC) $(CFLAGS) $< -o $@ $(LIB) $(LDFLAGS)

directories:
	@mkdir -p $(foreach dir,$(SRC_DIRS),obj/$(patsubst src/%,%,$(dir))) lib

clean:
	rm -rf obj lib tests/*.d
	find tests/ -type f ! -name "*.c" ! -name "*.h" -delete

install: $(LIB)
	install -d $(INCDIR)
	install -d $(LIBDIR)
	cp -r include/* $(INCDIR)/
	install -m 644 $(LIB) $(LIBDIR)/

uninstall:
	rm -rf $(INCDIR)/ttak
	rm -f $(LIBDIR)/libttak.a

blueprints:
	@python3 blueprints/scripts/render_blueprints.py

-include $(DEPS)

.PHONY: all clean directories install uninstall blueprints test

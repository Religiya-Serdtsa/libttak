CC ?= gcc
CXX ?= g++
AR ?= ar
NVCC ?= nvcc
HIPCC ?= hipcc
EMBEDDED ?= 0
USE_CUDA ?= 0
USE_OPENCL ?= 0
USE_ROCM ?= 0

# Toolchain selection: pass TOOLCHAIN=msvc on the command line to use cl/lib/link.
# GNU (GCC/Clang) is the default on all platforms.
TOOLCHAIN ?= gnu

COMMON_WARNINGS ?= -Wall -std=c17 -pthread -Iinclude -D_GNU_SOURCE -D_XOPEN_SOURCE=700
DEPFLAGS ?= -MD -MF $(@:.o=.d)
LDFLAGS_BASE = -pthread -lm

# Detect which compiler family we are using.
BUILD_PROFILE = perf
ifneq (,$(findstring tcc,$(notdir $(CC))))
BUILD_PROFILE = tcc
endif

# Clang detection and specific flags
ifneq (,$(findstring clang,$(notdir $(CC))))
PERF_STACK_FLAGS += -flto=thin -mllvm -inline-threshold=600
LDFLAGS += -flto=thin
endif

TCC_STACK_FLAGS ?= -O3 -g \
                  -fno-inline \
                  -fno-omit-frame-pointer \
                  -fno-optimize-sibling-calls \
				  -fno-semantic-interposition \
				  -fno-trapping-math \
				  -falign-functions=32 \
				  -fno-plt \
				  -fno-math-errno

PERF_WARNINGS ?= -Wextra -Wshadow -Wstrict-prototypes -Wswitch-enum
PERF_STACK_FLAGS ?= -O3 -march=native -mtune=native -pipe -flto -ffat-lto-objects \
                   -fomit-frame-pointer -funroll-loops \
                   -fstrict-aliasing -ffunction-sections -fdata-sections \
                   -fvisibility=hidden -DNDEBUG

ifeq ($(BUILD_PROFILE),tcc)
CFLAGS = $(COMMON_WARNINGS) $(TCC_STACK_FLAGS) -fPIC -ftls-model=global-dynamic
LDFLAGS = $(LDFLAGS_BASE)
else
CFLAGS = $(COMMON_WARNINGS) $(PERF_WARNINGS) $(PERF_STACK_FLAGS)
LDFLAGS = $(LDFLAGS_BASE) -flto -Wl,--gc-sections
endif

CFLAGS += $(EXTRA_CFLAGS) -DEMBEDDED=$(EMBEDDED)
LDFLAGS += $(EXTRA_LDFLAGS)

# ---- MSVC toolchain overrides (Windows / cl.exe) -----------------------
# Replaces GCC-only flags with cl-compatible equivalents.
ifeq ($(TOOLCHAIN),msvc)
CC         = cl
AR         = lib
LD         = link
DEPFLAGS   =
OBJEXT     = obj
LIBEXT     = lib
AR_EXTRA   =
AR_OUTFLAG = /OUT:
CFLAGS     = /O2 /W4 /GL /DNDEBUG /std:c17 /Iinclude $(EXTRA_CFLAGS) /DEMBEDDED=$(EMBEDDED)
LDFLAGS    = /LTCG $(EXTRA_LDFLAGS)
else
OBJEXT     = o
LIBEXT     = a
AR_EXTRA   = rcs
AR_OUTFLAG =
endif
# ------------------------------------------------------------------------

PREFIX ?= /usr/local
LIBDIR = $(PREFIX)/lib
INCDIR = $(PREFIX)/include

SRC_DIRS = src/ht src/thread src/timing src/mem src/async src/priority \
           src/atomic src/sync src/math src/tree src/container \
           src/security src/mem_tree src/limit src/stats src/log \
           src/unsafe src/shared src/mask src/phys/dimless src/io \
           src/net src/phys/mem src/script

SRCS = $(foreach dir,$(SRC_DIRS),$(wildcard $(dir)/*.c))
C_BASE_ACCEL_SRCS = src/accel/accel_cpu.c src/accel/ttak_accel_dispatch.c
SRCS += $(C_BASE_ACCEL_SRCS)
C_OPTIONAL_SRCS =
CUDA_SRCS =
ROCM_SRCS =

ifeq ($(USE_CUDA),1)
CUDA_SRCS += src/accel/accel_cuda.cu src/accel/bigint_cuda.cu
CFLAGS += -DENABLE_CUDA
ifeq ($(TOOLCHAIN),msvc)
NVCCFLAGS = -std=c++14 -Iinclude -DENABLE_CUDA -ccbin cl
else
NVCCFLAGS ?= -std=c++14 -Iinclude -DENABLE_CUDA
endif
endif

ifeq ($(USE_OPENCL),1)
C_OPTIONAL_SRCS += src/accel/accel_opencl.c src/accel/bigint_opencl.c
CFLAGS += -DENABLE_OPENCL
OPENCL_LIBS ?= -lOpenCL
LDFLAGS += $(OPENCL_LIBS)
endif

ifeq ($(USE_ROCM),1)
ROCM_SRCS += src/accel/accel_rocm.cpp src/accel/bigint_rocm.cpp
CFLAGS += -DENABLE_ROCM
HIPCCFLAGS ?= -std=c++17 -Iinclude -DENABLE_ROCM
endif

C_SRCS_ALL = $(SRCS) $(C_OPTIONAL_SRCS)
OBJS = $(patsubst src/%.c,obj/%.$(OBJEXT),$(SRCS))
OBJS += $(patsubst src/%.c,obj/%.$(OBJEXT),$(C_OPTIONAL_SRCS))
ASMS = $(patsubst src/%.c,obj/%.s,$(C_SRCS_ALL))

CUDA_OBJS = $(patsubst src/%.cu,obj/%.$(OBJEXT),$(CUDA_SRCS))
ROCM_OBJS = $(patsubst src/%.cpp,obj/%.$(OBJEXT),$(ROCM_SRCS))
OBJS += $(CUDA_OBJS) $(ROCM_OBJS)

DEPS = $(patsubst %.$(OBJEXT),%.d,$(OBJS))

LIB = lib/libttak.$(LIBEXT)

TEST_SRCS = $(wildcard tests/test_*.c)
TEST_BINS = $(TEST_SRCS:.c=)

all: directories $(LIB)

$(LIB): $(OBJS)
	@mkdir -p lib
	$(AR) $(AR_EXTRA) $(AR_OUTFLAG)$@ $(OBJS)

asm: directories $(ASMS)

# Default rule for all other directories
obj/%.o: src/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $(DEPFLAGS) -c $< -o $@

obj/%.obj: src/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) /c $< /Fo$@

obj/%.o: src/%.cu
	@mkdir -p $(dir $@)
	$(NVCC) $(NVCCFLAGS) -c $< -o $@

obj/%.obj: src/%.cu
	@mkdir -p $(dir $@)
	$(NVCC) $(NVCCFLAGS) -c $< -o $@

obj/%.o: src/%.cpp
	@mkdir -p $(dir $@)
	$(HIPCC) $(HIPCCFLAGS) -c $< -o $@

obj/%.s: obj/%.o
	@mkdir -p $(dir $@)
	objdump -d $< > $@

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
	@mkdir -p $(foreach dir,$(SRC_DIRS),obj/$(patsubst src/%,%,$(dir))) obj/accel lib

clean:
	rm -rf obj lib tests/*.d
	find tests/ -type f ! -name "*.c" ! -name "*.h" -delete
	rm -rf a.out

asm_clean:
	rm -f $(ASMS)

install: $(LIB)
	install -d $(INCDIR)
	install -d $(LIBDIR)
	cp -r include/* $(INCDIR)/
	install -m 644 $(LIB) $(LIBDIR)/

uninstall:
	rm -rf $(INCDIR)/ttak
	rm -f $(LIBDIR)/libttak.$(LIBEXT)

blueprints:
	@python3 blueprints/scripts/render_blueprints.py

-include $(DEPS)

.PHONY: all clean directories install uninstall blueprints test asm asm_clean

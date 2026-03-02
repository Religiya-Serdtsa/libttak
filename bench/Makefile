CC ?= gcc
USE_CUDA ?= 1
USE_OPENCL ?= 1
USE_ROCM ?= 0

TTAK_ROOT ?= ..
TTAK_INCLUDEDIR ?= $(TTAK_ROOT)/include
TTAK_LIBDIR ?= $(TTAK_ROOT)/lib
TTAK_STATIC_LIB ?= $(TTAK_LIBDIR)/libttak.a

ifneq ($(wildcard $(TTAK_STATIC_LIB)),)
FIRST_TTAK_OBJ := $(shell ar t $(TTAK_STATIC_LIB) 2>/dev/null | head -n 1)
ifneq ($(FIRST_TTAK_OBJ),)
LIBTTK_MAGIC := $(shell ar p $(TTAK_STATIC_LIB) $(FIRST_TTAK_OBJ) 2>/dev/null | head -c 4 | od -An -t x1 | tr -d ' \n')
ifeq ($(LIBTTK_MAGIC),4243c0de)
CLANG_BIN := $(shell command -v clang 2>/dev/null)
ifneq ($(CLANG_BIN),)
CC := $(CLANG_BIN)
endif
endif
endif
endif

CFLAGS = -Wall -std=c17 -pthread -I$(TTAK_INCLUDEDIR) -O3 -march=native -flto -D_GNU_SOURCE
LDFLAGS = -L$(TTAK_LIBDIR) -lttak -lm -pthread -flto

OPENCL_LIBS ?= -lOpenCL
CUDA_HOME ?= /usr/local/cuda
CUDA_LIBDIR ?= $(CUDA_HOME)/lib64
CUDA_LIBS ?= -lcudart -lstdc++
ROCM_LIBS ?= -lamdhip64 -lstdc++

ifeq ($(USE_OPENCL),1)
LDFLAGS += $(OPENCL_LIBS)
endif

ifeq ($(USE_CUDA),1)
LDFLAGS += -L$(CUDA_LIBDIR) $(CUDA_LIBS)
endif

ifeq ($(USE_ROCM),1)
LDFLAGS += $(ROCM_LIBS)
endif

TARGETS = ttak_bench

all: $(TARGETS)

ttak_bench: ttak_bench.c
	$(CC) $(CFLAGS) $< -o $@ $(LDFLAGS)

clean:
	rm -f $(TARGETS)

.PHONY: all clean

CC ?= gcc
USE_CUDA ?= 1
USE_OPENCL ?= 1
USE_ROCM ?= 0

CFLAGS = -Wall -std=c17 -pthread -I../include -O3 -march=native -flto -D_GNU_SOURCE
LDFLAGS = -L../lib -lttak -lm -pthread -flto

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

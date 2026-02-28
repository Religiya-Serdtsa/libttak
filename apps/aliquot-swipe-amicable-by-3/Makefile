CC ?= tcc
# Build the aliquot swipe (amicable-by-3) against the in-tree libttak.
ROOT ?= ../..
LIBTTK ?= $(ROOT)/lib/libttak.a

ACCEL ?= cpu
ACCEL_MODE := $(shell echo $(ACCEL) | tr '[:upper:]' '[:lower:]')

ACCEL_CPPFLAGS :=
ACCEL_LDFLAGS :=
LIBTTK_ACCEL_FLAGS :=
ACCEL_LABEL := CPU

ifeq ($(ACCEL_MODE),cuda)
ACCEL_CPPFLAGS += -DENABLE_CUDA
ACCEL_LDFLAGS += -lcuda -L$(ROOT)/lib -L/usr/local/cuda-13.1/targets/x86_64-linux/lib -lttak -pthread -lm -lcudart -lstdc++ -D_TTAK_BIGINT_ACCEL_THRESHOLD=1

LIBTTK_ACCEL_FLAGS = USE_CUDA=1 USE_OPENCL=0 USE_ROCM=0
ACCEL_LABEL := CUDA
else ifeq ($(ACCEL_MODE),rocm)
ACCEL_CPPFLAGS += -DENABLE_ROCM
ACCEL_LDFLAGS += -lamdhip64
LIBTTK_ACCEL_FLAGS = USE_CUDA=0 USE_OPENCL=0 USE_ROCM=1
ACCEL_LABEL := ROCm
else ifeq ($(ACCEL_MODE),opencl)
ACCEL_CPPFLAGS += -DENABLE_OPENCL
ACCEL_LDFLAGS += -lOpenCL
LIBTTK_ACCEL_FLAGS = USE_CUDA=0 USE_OPENCL=1 USE_ROCM=0
ACCEL_LABEL := OpenCL
else ifeq ($(ACCEL_MODE),cpu)
ACCEL_CPPFLAGS += -DENABLE_CPU_ONLY
LIBTTK_ACCEL_FLAGS = USE_CUDA=0 USE_OPENCL=0 USE_ROCM=0
ACCEL_LABEL := CPU
else
$(error Unknown ACCEL backend '$(ACCEL)')
endif

CPPFLAGS += -I$(ROOT)/include $(ACCEL_CPPFLAGS) -DALIQUOT_ACCEL_LABEL=\"$(ACCEL_LABEL)\"
CFLAGS += -std=c11 -Wall
LDFLAGS += -L$(ROOT)/lib -lttak -pthread -lm $(ACCEL_LDFLAGS)

BIN_DIR = bin
SRC_DIR = src
TARGET = $(BIN_DIR)/aliquot_swipe

SRCS = $(SRC_DIR)/main.c
OBJS = $(SRCS:.c=.o)

CROSS_VERIFY_TARGET = $(BIN_DIR)/aliquot_cross_verify
CROSS_VERIFY_SRCS = $(SRC_DIR)/cross_verify.c
CROSS_VERIFY_OBJS = $(CROSS_VERIFY_SRCS:.c=.o)

all: $(TARGET) $(CROSS_VERIFY_TARGET)

$(TARGET): $(OBJS) $(LIBTTK)
	@mkdir -p $(BIN_DIR)
	$(CC) $(CFLAGS) $(OBJS) -o $@ $(LDFLAGS)

$(CROSS_VERIFY_TARGET): $(CROSS_VERIFY_OBJS) $(LIBTTK)
	@mkdir -p $(BIN_DIR)
	$(CC) $(CFLAGS) $(CROSS_VERIFY_OBJS) -o $@ $(LDFLAGS)

%.o: %.c
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@

$(LIBTTK):
	$(MAKE) -C $(ROOT) $(LIBTTK_ACCEL_FLAGS) lib/libttak.a

clean:
	rm -f $(OBJS) $(TARGET) $(CROSS_VERIFY_OBJS) $(CROSS_VERIFY_TARGET)

.PHONY: all clean cross_verify

cross_verify: $(CROSS_VERIFY_TARGET)

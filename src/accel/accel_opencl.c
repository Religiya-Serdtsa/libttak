#include "ttak/ttak_accelerator.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

extern ttak_result_t ttak_accel_run_cpu(
    const ttak_accel_batch_item_t *items,
    size_t item_count,
    const ttak_accel_config_t *config);

#ifdef ENABLE_OPENCL
#include <CL/cl.h>

#define TTAK_STR_HELPER(x) #x
#define TTAK_STR(x) TTAK_STR_HELPER(x)

#define TTAK_ACCEL_FACTOR_MAX 64

static inline uint32_t ttak_guard_word(const ttak_accel_config_t *config,
                                       const ttak_accel_batch_item_t *item) {
    uint32_t guard = config->integrity_mask ^ item->mask_seed;
    guard |= 0x01010101u;
    return guard;
}

static inline uint32_t ttak_checksum_seed(const ttak_accel_batch_item_t *item) {
    return (item->checksum_salt == 0u) ? 2166136261u : item->checksum_salt;
}

typedef struct {
    uint64_t prime;
    uint32_t exponent;
    uint32_t reserved;
} ttak_accel_factor_slot_t;

typedef struct {
    uint64_t value;
    uint32_t factor_count;
    uint32_t checksum;
    uint32_t reserved;
    ttak_accel_factor_slot_t slots[TTAK_ACCEL_FACTOR_MAX];
} ttak_accel_factor_record_t;

typedef struct {
    uint32_t guard;
    uint32_t record_count;
    uint32_t payload_checksum;
    uint32_t reserved;
} ttak_accel_record_prefix_t;

static inline uint32_t ttak_fnv1a32(const void *data, size_t len, uint32_t seed) {
    const uint8_t *ptr = (const uint8_t *)data;
    uint32_t hash = (seed == 0u) ? 2166136261u : seed;
    for (size_t i = 0; i < len; ++i) {
        hash ^= ptr[i];
        hash *= 16777619u;
    }
    return hash;
}

static inline void ttak_mask_payload(uint8_t *buf, size_t len, uint32_t guard) {
    const uint8_t lanes[4] = {
        (uint8_t)(guard & 0xFFu),
        (uint8_t)((guard >> 8) & 0xFFu),
        (uint8_t)((guard >> 16) & 0xFFu),
        (uint8_t)((guard >> 24) & 0xFFu)
    };
    for (size_t i = 0; i < len; ++i) {
        buf[i] ^= lanes[i & 0x3u];
    }
}

static void ttak_finalize_record(ttak_accel_factor_record_t *record,
                                 uint32_t checksum_seed,
                                 uint32_t ordinal) {
    record->reserved = 0;
    uint32_t seed = checksum_seed ^ (ordinal * 0x9E3779B1u);
    uint32_t hash = ttak_fnv1a32(&record->value,
                                 sizeof(record->value) + sizeof(record->factor_count) + sizeof(record->reserved),
                                 seed);
    hash = ttak_fnv1a32(record->slots, sizeof(record->slots), hash);
    record->checksum = hash;
}

static ttak_result_t ttak_finalize_output(const ttak_accel_batch_item_t *item,
                                          uint32_t guard,
                                          size_t record_count,
                                          uint32_t checksum_seed) {
    size_t payload_offset = sizeof(ttak_accel_record_prefix_t);
    size_t payload_size = record_count * sizeof(ttak_accel_factor_record_t);
    uint8_t *payload = item->output + payload_offset;
    uint32_t payload_checksum = ttak_fnv1a32(payload, payload_size, checksum_seed);

    ttak_accel_record_prefix_t prefix = {
        .guard = guard,
        .record_count = (uint32_t)record_count,
        .payload_checksum = payload_checksum,
        .reserved = sizeof(ttak_accel_factor_record_t)
    };
    memcpy(item->output, &prefix, sizeof(prefix));
    ttak_mask_payload(payload, payload_size, guard);
    if (item->checksum_out != NULL) {
        *(item->checksum_out) = payload_checksum;
    }
    return TTAK_RESULT_OK;
}

static const char *kFactorKernelSrc =
"typedef struct { ulong prime; uint exponent; uint reserved; } accel_slot;\n"
"typedef struct {\n"
"    ulong value;\n"
"    uint factor_count;\n"
"    uint checksum;\n"
"    uint reserved;\n"
"    accel_slot slots[" TTAK_STR(TTAK_ACCEL_FACTOR_MAX) "];\n"
"} accel_record;\n"
"inline void add_factor(accel_record *rec, ulong prime) {\n"
"    for (uint i = 0; i < rec->factor_count; ++i) {\n"
"        if (rec->slots[i].prime == prime) {\n"
"            rec->slots[i].exponent++;\n"
"            return;\n"
"        }\n"
"    }\n"
"    if (rec->factor_count >= " TTAK_STR(TTAK_ACCEL_FACTOR_MAX) ") {\n"
"        rec->slots[" TTAK_STR(TTAK_ACCEL_FACTOR_MAX) " - 1].prime = prime;\n"
"        rec->slots[" TTAK_STR(TTAK_ACCEL_FACTOR_MAX) " - 1].exponent = 0xFFFFFFFFu;\n"
"        return;\n"
"    }\n"
"    uint idx = rec->factor_count++;\n"
"    rec->slots[idx].prime = prime;\n"
"    rec->slots[idx].exponent = 1u;\n"
"}\n"
"__kernel void factor_kernel(__global const ulong *values,\n"
"                            __global accel_record *records,\n"
"                            ulong count) {\n"
"    ulong idx = get_global_id(0);\n"
"    if (idx >= count) return;\n"
"    accel_record rec;\n"
"    rec.value = values[idx];\n"
"    rec.factor_count = 0u;\n"
"    rec.checksum = 0u;\n"
"    rec.reserved = 0u;\n"
"    for (int i = 0; i < " TTAK_STR(TTAK_ACCEL_FACTOR_MAX) "; ++i) {\n"
"        rec.slots[i].prime = 0ul;\n"
"        rec.slots[i].exponent = 0u;\n"
"        rec.slots[i].reserved = 0u;\n"
"    }\n"
"    ulong n = rec.value;\n"
"    if (n > 1ul) {\n"
"        while ((n & 1ul) == 0ul) {\n"
"            add_factor(&rec, 2ul);\n"
"            n >>= 1ul;\n"
"        }\n"
"        for (ulong p = 3ul; p <= n / p; p += 2ul) {\n"
"            while (n % p == 0ul) {\n"
"                add_factor(&rec, p);\n"
"                n /= p;\n"
"            }\n"
"        }\n"
"        if (n > 1ul) {\n"
"            add_factor(&rec, n);\n"
"        }\n"
"    }\n"
"    records[idx] = rec;\n"
"}\n";

typedef struct {
    cl_context context;
    cl_command_queue queue;
    cl_program program;
    cl_kernel kernel;
    cl_device_id device;
    bool ready;
} ttak_opencl_context_t;

static ttak_opencl_context_t g_ocl = {0};

static void ttak_opencl_release(void) {
    if (g_ocl.kernel) {
        clReleaseKernel(g_ocl.kernel);
        g_ocl.kernel = NULL;
    }
    if (g_ocl.program) {
        clReleaseProgram(g_ocl.program);
        g_ocl.program = NULL;
    }
    if (g_ocl.queue) {
        clReleaseCommandQueue(g_ocl.queue);
        g_ocl.queue = NULL;
    }
    if (g_ocl.context) {
        clReleaseContext(g_ocl.context);
        g_ocl.context = NULL;
    }
    g_ocl.ready = false;
}

static bool ttak_opencl_build(void) {
    if (g_ocl.ready) return true;

    cl_int err = CL_SUCCESS;
    cl_uint platform_count = 0;
    if (clGetPlatformIDs(0, NULL, &platform_count) != CL_SUCCESS || platform_count == 0) {
        return false;
    }

    cl_platform_id platform = NULL;
    if (clGetPlatformIDs(1, &platform, NULL) != CL_SUCCESS) {
        return false;
    }

    cl_uint device_count = 0;
    err = clGetDeviceIDs(platform, CL_DEVICE_TYPE_GPU, 1, &g_ocl.device, &device_count);
    if (err != CL_SUCCESS) {
        err = clGetDeviceIDs(platform, CL_DEVICE_TYPE_CPU, 1, &g_ocl.device, &device_count);
        if (err != CL_SUCCESS) {
            return false;
        }
    }

    g_ocl.context = clCreateContext(NULL, 1, &g_ocl.device, NULL, NULL, &err);
    if (err != CL_SUCCESS || g_ocl.context == NULL) {
        ttak_opencl_release();
        return false;
    }

#if defined(CL_TARGET_OPENCL_VERSION) && (CL_TARGET_OPENCL_VERSION >= 200)
    g_ocl.queue = clCreateCommandQueueWithProperties(g_ocl.context, g_ocl.device, NULL, &err);
#else
    g_ocl.queue = clCreateCommandQueue(g_ocl.context, g_ocl.device, 0, &err);
#endif
    if (err != CL_SUCCESS || g_ocl.queue == NULL) {
        ttak_opencl_release();
        return false;
    }

    const char *src = kFactorKernelSrc;
    size_t len = strlen(kFactorKernelSrc);
    g_ocl.program = clCreateProgramWithSource(g_ocl.context, 1, &src, &len, &err);
    if (err != CL_SUCCESS || g_ocl.program == NULL) {
        ttak_opencl_release();
        return false;
    }

    err = clBuildProgram(g_ocl.program, 1, &g_ocl.device, NULL, NULL, NULL);
    if (err != CL_SUCCESS) {
        ttak_opencl_release();
        return false;
    }

    g_ocl.kernel = clCreateKernel(g_ocl.program, "factor_kernel", &err);
    if (err != CL_SUCCESS || g_ocl.kernel == NULL) {
        ttak_opencl_release();
        return false;
    }

    g_ocl.ready = true;
    return true;
}

static ttak_result_t ttak_opencl_process_item(const ttak_accel_batch_item_t *item,
                                              const ttak_accel_config_t *config) {
    if (item->input == NULL || item->output == NULL) {
        return TTAK_RESULT_ERR_ARGUMENT;
    }
    if (item->input_len == 0 || (item->input_len % sizeof(uint64_t)) != 0) {
        return TTAK_RESULT_ERR_ARGUMENT;
    }

    size_t record_count = item->input_len / sizeof(uint64_t);
    if (record_count == 0 || record_count > UINT32_MAX) {
        return TTAK_RESULT_ERR_ARGUMENT;
    }
    if (record_count > (SIZE_MAX - sizeof(ttak_accel_record_prefix_t)) /
                           sizeof(ttak_accel_factor_record_t)) {
        return TTAK_RESULT_ERR_ARGUMENT;
    }

    size_t payload_size = record_count * sizeof(ttak_accel_factor_record_t);
    size_t needed = sizeof(ttak_accel_record_prefix_t) + payload_size;
    if (item->output_len < needed) {
        return TTAK_RESULT_ERR_ARGUMENT;
    }

    uint32_t guard = ttak_guard_word(config, item);
    uint32_t checksum_seed = ttak_checksum_seed(item);

    size_t value_bytes = record_count * sizeof(uint64_t);
    uint64_t *host_values = (uint64_t *)malloc(value_bytes);
    ttak_accel_factor_record_t *host_records =
        (ttak_accel_factor_record_t *)malloc(payload_size);
    if (!host_values || !host_records) {
        free(host_values);
        free(host_records);
        return TTAK_RESULT_ERR_EXECUTION;
    }

    for (size_t idx = 0; idx < record_count; ++idx) {
        memcpy(&host_values[idx],
               item->input + idx * sizeof(uint64_t),
               sizeof(uint64_t));
    }

    cl_int err = CL_SUCCESS;
    cl_mem values_buf = clCreateBuffer(g_ocl.context,
                                       CL_MEM_READ_ONLY | CL_MEM_COPY_HOST_PTR,
                                       value_bytes,
                                       host_values,
                                       &err);
    if (err != CL_SUCCESS || values_buf == NULL) {
        free(host_values);
        free(host_records);
        return TTAK_RESULT_ERR_EXECUTION;
    }

    cl_mem records_buf = clCreateBuffer(g_ocl.context,
                                        CL_MEM_WRITE_ONLY,
                                        payload_size,
                                        NULL,
                                        &err);
    if (err != CL_SUCCESS || records_buf == NULL) {
        clReleaseMemObject(values_buf);
        free(host_values);
        free(host_records);
        return TTAK_RESULT_ERR_EXECUTION;
    }

    err = clSetKernelArg(g_ocl.kernel, 0, sizeof(cl_mem), &values_buf);
    err |= clSetKernelArg(g_ocl.kernel, 1, sizeof(cl_mem), &records_buf);
    cl_ulong count_arg = (cl_ulong)record_count;
    err |= clSetKernelArg(g_ocl.kernel, 2, sizeof(cl_ulong), &count_arg);
    if (err != CL_SUCCESS) {
        clReleaseMemObject(values_buf);
        clReleaseMemObject(records_buf);
        free(host_values);
        free(host_records);
        return TTAK_RESULT_ERR_EXECUTION;
    }

    size_t local = 64;
    size_t global = ((record_count + local - 1) / local) * local;
    err = clEnqueueNDRangeKernel(g_ocl.queue, g_ocl.kernel, 1, NULL, &global, &local, 0, NULL, NULL);
    if (err != CL_SUCCESS) {
        clReleaseMemObject(values_buf);
        clReleaseMemObject(records_buf);
        free(host_values);
        free(host_records);
        return TTAK_RESULT_ERR_EXECUTION;
    }

    err = clFinish(g_ocl.queue);
    if (err != CL_SUCCESS) {
        clReleaseMemObject(values_buf);
        clReleaseMemObject(records_buf);
        free(host_values);
        free(host_records);
        return TTAK_RESULT_ERR_EXECUTION;
    }

    err = clEnqueueReadBuffer(g_ocl.queue,
                              records_buf,
                              CL_TRUE,
                              0,
                              payload_size,
                              host_records,
                              0,
                              NULL,
                              NULL);
    clReleaseMemObject(values_buf);
    clReleaseMemObject(records_buf);
    free(host_values);
    if (err != CL_SUCCESS) {
        free(host_records);
        return TTAK_RESULT_ERR_EXECUTION;
    }

    for (size_t idx = 0; idx < record_count; ++idx) {
        ttak_finalize_record(&host_records[idx], checksum_seed, (uint32_t)idx);
        memcpy(item->output + sizeof(ttak_accel_record_prefix_t) +
                   idx * sizeof(ttak_accel_factor_record_t),
               &host_records[idx],
               sizeof(ttak_accel_factor_record_t));
    }

    free(host_records);
    return ttak_finalize_output(item, guard, record_count, checksum_seed);
}

static ttak_result_t ttak_opencl_dispatch(const ttak_accel_batch_item_t *items,
                                          size_t item_count,
                                          const ttak_accel_config_t *config) {
    if (!ttak_opencl_build()) {
        return TTAK_RESULT_ERR_EXECUTION;
    }

    for (size_t idx = 0; idx < item_count; ++idx) {
        ttak_result_t status = ttak_opencl_process_item(&items[idx], config);
        if (status != TTAK_RESULT_OK) {
            return status;
        }
    }
    return TTAK_RESULT_OK;
}

ttak_result_t ttak_accel_run_opencl(
    const ttak_accel_batch_item_t *items,
    size_t item_count,
    const ttak_accel_config_t *config) {
    if (items == NULL || config == NULL) {
        return TTAK_RESULT_ERR_ARGUMENT;
    }

    ttak_result_t status = ttak_opencl_dispatch(items, item_count, config);
    if (status != TTAK_RESULT_OK) {
        return ttak_accel_run_cpu(items, item_count, config);
    }
    return status;
}

#else

ttak_result_t ttak_accel_run_opencl(
    const ttak_accel_batch_item_t *items,
    size_t item_count,
    const ttak_accel_config_t *config) {
    return ttak_accel_run_cpu(items, item_count, config);
}

#endif

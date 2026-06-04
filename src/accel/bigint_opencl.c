/**
 * @file bigint_opencl.c
 * @brief OpenCL kernel dispatch for arbitrary-precision integer operations.
 *
 * Provides GPU-accelerated multiplication and modular reduction for
 * ttak_bigint_t operands above the threshold set in bigint_accel.c.
 */

#include <ttak/math/bigint.h>

#ifdef ENABLE_OPENCL

#include <CL/cl.h>

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    cl_context context;
    cl_command_queue queue;
    cl_program program;
    cl_kernel add_kernel;
    cl_kernel mul_kernel;
    cl_device_id device;
    bool ready;
} ttak_bigint_opencl_ctx_t;

static ttak_bigint_opencl_ctx_t g_bigint_ocl = {0};

static const char *kBigIntKernelSrc =
"__kernel void bigint_add(__global const uint *lhs, uint lhs_len,\n"
"                         __global const uint *rhs, uint rhs_len,\n"
"                         __global ulong *temp, uint temp_len) {\n"
"    uint idx = get_global_id(0);\n"
"    uint stride = get_global_size(0);\n"
"    for (uint i = idx; i < temp_len; i += stride) {\n"
"        ulong sum = 0;\n"
"        if (i < lhs_len) sum += (ulong)lhs[i];\n"
"        if (i < rhs_len) sum += (ulong)rhs[i];\n"
"        temp[i] = sum;\n"
"    }\n"
"}\n"
"\n"
"typedef struct { ulong lo; ulong hi; } u128;\n"
"inline u128 u128_add(u128 a, u128 b) {\n"
"    u128 r;\n"
"    r.lo = a.lo + b.lo;\n"
"    r.hi = a.hi + b.hi + (r.lo < a.lo);\n"
"    return r;\n"
"}\n"
"inline u128 u128_from_u64(ulong x) {\n"
"    u128 r; r.lo = x; r.hi = 0; return r;\n"
"}\n"
"__kernel void bigint_mul(__global const uint *lhs, uint lhs_len,\n"
"                         __global const uint *rhs, uint rhs_len,\n"
"                         __global ulong *temp_lo,\n"
"                         __global ulong *temp_hi, uint temp_len) {\n"
"    uint k = get_global_id(0);\n"
"    if (k >= temp_len) return;\n"
"    u128 sum = u128_from_u64(0);\n"
"    for (uint i = 0; i < lhs_len && i <= k; ++i) {\n"
"        uint j = k - i;\n"
"        if (j >= rhs_len) continue;\n"
"        u128 prod = u128_from_u64((ulong)lhs[i] * (ulong)rhs[j]);\n"
"        sum = u128_add(sum, prod);\n"
"    }\n"
"    temp_lo[k] = sum.lo;\n"
"    temp_hi[k] = sum.hi;\n"
"}\n";

static void ttak_bigint_opencl_release(void) {
    if (g_bigint_ocl.add_kernel) clReleaseKernel(g_bigint_ocl.add_kernel);
    if (g_bigint_ocl.mul_kernel) clReleaseKernel(g_bigint_ocl.mul_kernel);
    if (g_bigint_ocl.program) clReleaseProgram(g_bigint_ocl.program);
    if (g_bigint_ocl.queue) clReleaseCommandQueue(g_bigint_ocl.queue);
    if (g_bigint_ocl.context) clReleaseContext(g_bigint_ocl.context);
    g_bigint_ocl.add_kernel = NULL;
    g_bigint_ocl.mul_kernel = NULL;
    g_bigint_ocl.program = NULL;
    g_bigint_ocl.queue = NULL;
    g_bigint_ocl.context = NULL;
    g_bigint_ocl.ready = false;
}

static bool ttak_bigint_opencl_build(void) {
    if (g_bigint_ocl.ready) return true;

    cl_int err = CL_SUCCESS;
    cl_platform_id platform = NULL;
    cl_uint platform_count = 0;
    if (clGetPlatformIDs(1, &platform, &platform_count) != CL_SUCCESS || platform_count == 0) {
        return false;
    }

    cl_uint device_count = 0;
    err = clGetDeviceIDs(platform, CL_DEVICE_TYPE_GPU, 1, &g_bigint_ocl.device, &device_count);
    if (err != CL_SUCCESS) {
        err = clGetDeviceIDs(platform, CL_DEVICE_TYPE_CPU, 1, &g_bigint_ocl.device, &device_count);
        if (err != CL_SUCCESS) {
            return false;
        }
    }

    g_bigint_ocl.context = clCreateContext(NULL, 1, &g_bigint_ocl.device, NULL, NULL, &err);
    if (err != CL_SUCCESS || g_bigint_ocl.context == NULL) {
        ttak_bigint_opencl_release();
        return false;
    }

#if defined(CL_TARGET_OPENCL_VERSION) && (CL_TARGET_OPENCL_VERSION >= 200)
    g_bigint_ocl.queue = clCreateCommandQueueWithProperties(g_bigint_ocl.context, g_bigint_ocl.device, NULL, &err);
#else
    g_bigint_ocl.queue = clCreateCommandQueue(g_bigint_ocl.context, g_bigint_ocl.device, 0, &err);
#endif
    if (err != CL_SUCCESS || g_bigint_ocl.queue == NULL) {
        ttak_bigint_opencl_release();
        return false;
    }

    size_t src_len = strlen(kBigIntKernelSrc);
    const char *src_ptr = kBigIntKernelSrc;
    g_bigint_ocl.program = clCreateProgramWithSource(g_bigint_ocl.context, 1, &src_ptr, &src_len, &err);
    if (err != CL_SUCCESS || g_bigint_ocl.program == NULL) {
        ttak_bigint_opencl_release();
        return false;
    }

    err = clBuildProgram(g_bigint_ocl.program, 1, &g_bigint_ocl.device, NULL, NULL, NULL);
    if (err != CL_SUCCESS) {
        ttak_bigint_opencl_release();
        return false;
    }

    g_bigint_ocl.add_kernel = clCreateKernel(g_bigint_ocl.program, "bigint_add", &err);
    if (err != CL_SUCCESS || g_bigint_ocl.add_kernel == NULL) {
        ttak_bigint_opencl_release();
        return false;
    }
    g_bigint_ocl.mul_kernel = clCreateKernel(g_bigint_ocl.program, "bigint_mul", &err);
    if (err != CL_SUCCESS || g_bigint_ocl.mul_kernel == NULL) {
        ttak_bigint_opencl_release();
        return false;
    }

    g_bigint_ocl.ready = true;
    return true;
}

static cl_mem ttak_bigint_opencl_buffer_from_host(const limb_t *src,
                                                  size_t count,
                                                  cl_mem_flags flags,
                                                  cl_int *out_err) {
    cl_uint zero = 0;
    size_t bytes = (count > 0) ? (count * sizeof(cl_uint)) : sizeof(cl_uint);
    const void *host_ptr = (count > 0 && src) ? (const void *)src : (const void *)&zero;
    return clCreateBuffer(g_bigint_ocl.context,
                          flags | CL_MEM_COPY_HOST_PTR,
                          bytes,
                          (void *)host_ptr,
                          out_err);
}

static void ttak_bigint_opencl_trim(const limb_t *src, size_t len, size_t *out_used) {
    size_t used = len;
    while (used > 0 && src[used - 1] == 0) {
        --used;
    }
    if (out_used) *out_used = used;
}

bool ttak_bigint_accel_opencl_add(limb_t *dst,
                                  size_t dst_capacity,
                                  size_t *out_used,
                                  const limb_t *lhs,
                                  size_t lhs_used,
                                  const limb_t *rhs,
                                  size_t rhs_used) {
    if (!ttak_bigint_opencl_build()) return false;

    size_t max_used = lhs_used > rhs_used ? lhs_used : rhs_used;
    size_t result_len = max_used + 1;
    if (dst_capacity < result_len) {
        return false;
    }

    cl_int err = CL_SUCCESS;
    cl_mem lhs_buf = ttak_bigint_opencl_buffer_from_host(lhs, lhs_used, CL_MEM_READ_ONLY, &err);
    if (err != CL_SUCCESS) return false;
    cl_mem rhs_buf = ttak_bigint_opencl_buffer_from_host(rhs, rhs_used, CL_MEM_READ_ONLY, &err);
    if (err != CL_SUCCESS) {
        clReleaseMemObject(lhs_buf);
        return false;
    }
    cl_mem temp_buf = clCreateBuffer(g_bigint_ocl.context, CL_MEM_WRITE_ONLY, result_len * sizeof(cl_ulong), NULL, &err);
    if (err != CL_SUCCESS) {
        clReleaseMemObject(lhs_buf);
        clReleaseMemObject(rhs_buf);
        return false;
    }

    cl_kernel kernel = g_bigint_ocl.add_kernel;
    err  = clSetKernelArg(kernel, 0, sizeof(cl_mem), &lhs_buf);
    err |= clSetKernelArg(kernel, 1, sizeof(cl_uint), &(cl_uint){ (cl_uint)lhs_used });
    err |= clSetKernelArg(kernel, 2, sizeof(cl_mem), &rhs_buf);
    err |= clSetKernelArg(kernel, 3, sizeof(cl_uint), &(cl_uint){ (cl_uint)rhs_used });
    err |= clSetKernelArg(kernel, 4, sizeof(cl_mem), &temp_buf);
    err |= clSetKernelArg(kernel, 5, sizeof(cl_uint), &(cl_uint){ (cl_uint)result_len });
    if (err != CL_SUCCESS) {
        clReleaseMemObject(lhs_buf);
        clReleaseMemObject(rhs_buf);
        clReleaseMemObject(temp_buf);
        return false;
    }

    size_t local = 64;
    size_t global = ((result_len + local - 1) / local) * local;
    err = clEnqueueNDRangeKernel(g_bigint_ocl.queue, kernel, 1, NULL, &global, &local, 0, NULL, NULL);
    if (err == CL_SUCCESS) {
        err = clFinish(g_bigint_ocl.queue);
    }

    bool ok = (err == CL_SUCCESS);
    cl_ulong *temp = NULL;
    if (ok) {
        temp = (cl_ulong *)malloc(result_len * sizeof(cl_ulong));
        if (temp) {
            err = clEnqueueReadBuffer(g_bigint_ocl.queue, temp_buf, CL_TRUE, 0,
                                      result_len * sizeof(cl_ulong), temp, 0, NULL, NULL);
            ok = (err == CL_SUCCESS);
        } else {
            ok = false;
        }
    }

    clReleaseMemObject(lhs_buf);
    clReleaseMemObject(rhs_buf);
    clReleaseMemObject(temp_buf);

    if (!ok) {
        free(temp);
        return false;
    }

    cl_ulong carry = 0;
    for (size_t i = 0; i < result_len; ++i) {
        cl_ulong v = temp[i] + carry;
        dst[i] = (limb_t)v;
        carry = v >> 32;
    }
    free(temp);

    ttak_bigint_opencl_trim(dst, result_len, out_used);
    return true;
}

bool ttak_bigint_accel_opencl_mul(limb_t *dst,
                                  size_t dst_capacity,
                                  size_t *out_used,
                                  const limb_t *lhs,
                                  size_t lhs_used,
                                  const limb_t *rhs,
                                  size_t rhs_used) {
    if (!ttak_bigint_opencl_build()) return false;
    size_t result_len = lhs_used + rhs_used;
    if (result_len == 0 || dst_capacity < result_len) {
        return false;
    }

    cl_int err = CL_SUCCESS;
    cl_mem lhs_buf = ttak_bigint_opencl_buffer_from_host(lhs, lhs_used, CL_MEM_READ_ONLY, &err);
    if (err != CL_SUCCESS) return false;
    cl_mem rhs_buf = ttak_bigint_opencl_buffer_from_host(rhs, rhs_used, CL_MEM_READ_ONLY, &err);
    if (err != CL_SUCCESS) {
        clReleaseMemObject(lhs_buf);
        return false;
    }
    cl_mem temp_lo_buf = clCreateBuffer(g_bigint_ocl.context, CL_MEM_WRITE_ONLY, result_len * sizeof(cl_ulong), NULL, &err);
    if (err != CL_SUCCESS) {
        clReleaseMemObject(lhs_buf);
        clReleaseMemObject(rhs_buf);
        return false;
    }
    cl_mem temp_hi_buf = clCreateBuffer(g_bigint_ocl.context, CL_MEM_WRITE_ONLY, result_len * sizeof(cl_ulong), NULL, &err);
    if (err != CL_SUCCESS) {
        clReleaseMemObject(lhs_buf);
        clReleaseMemObject(rhs_buf);
        clReleaseMemObject(temp_lo_buf);
        return false;
    }

    cl_kernel kernel = g_bigint_ocl.mul_kernel;
    err  = clSetKernelArg(kernel, 0, sizeof(cl_mem), &lhs_buf);
    err |= clSetKernelArg(kernel, 1, sizeof(cl_uint), &(cl_uint){ (cl_uint)lhs_used });
    err |= clSetKernelArg(kernel, 2, sizeof(cl_mem), &rhs_buf);
    err |= clSetKernelArg(kernel, 3, sizeof(cl_uint), &(cl_uint){ (cl_uint)rhs_used });
    err |= clSetKernelArg(kernel, 4, sizeof(cl_mem), &temp_lo_buf);
    err |= clSetKernelArg(kernel, 5, sizeof(cl_mem), &temp_hi_buf);
    err |= clSetKernelArg(kernel, 6, sizeof(cl_uint), &(cl_uint){ (cl_uint)result_len });
    if (err != CL_SUCCESS) {
        clReleaseMemObject(lhs_buf);
        clReleaseMemObject(rhs_buf);
        clReleaseMemObject(temp_lo_buf);
        clReleaseMemObject(temp_hi_buf);
        return false;
    }

    size_t local = 64;
    size_t global = ((result_len + local - 1) / local) * local;
    err = clEnqueueNDRangeKernel(g_bigint_ocl.queue, kernel, 1, NULL, &global, &local, 0, NULL, NULL);
    if (err == CL_SUCCESS) {
        err = clFinish(g_bigint_ocl.queue);
    }

    bool ok = (err == CL_SUCCESS);
    cl_ulong *temp_lo = NULL;
    cl_ulong *temp_hi = NULL;
    if (ok) {
        temp_lo = (cl_ulong *)malloc(result_len * sizeof(cl_ulong));
        temp_hi = (cl_ulong *)malloc(result_len * sizeof(cl_ulong));
        if (temp_lo && temp_hi) {
            err = clEnqueueReadBuffer(g_bigint_ocl.queue, temp_lo_buf, CL_TRUE, 0,
                                      result_len * sizeof(cl_ulong), temp_lo, 0, NULL, NULL);
            ok = (err == CL_SUCCESS);
            if (ok) {
                err = clEnqueueReadBuffer(g_bigint_ocl.queue, temp_hi_buf, CL_TRUE, 0,
                                          result_len * sizeof(cl_ulong), temp_hi, 0, NULL, NULL);
                ok = (err == CL_SUCCESS);
            }
        } else {
            ok = false;
        }
    }

    clReleaseMemObject(lhs_buf);
    clReleaseMemObject(rhs_buf);
    clReleaseMemObject(temp_lo_buf);
    clReleaseMemObject(temp_hi_buf);

    if (!ok) {
        free(temp_lo);
        free(temp_hi);
        return false;
    }

    cl_ulong carry = 0;
    for (size_t i = 0; i < result_len; ++i) {
        cl_ulong lo = temp_lo[i] + carry;
        cl_ulong hi = temp_hi[i] + (lo < carry);
        dst[i] = (limb_t)(lo & 0xFFFFFFFFUL);
        carry = (hi << 32) | (lo >> 32);
    }
    free(temp_lo);
    free(temp_hi);

    ttak_bigint_opencl_trim(dst, result_len, out_used);
    return true;
}

#endif /* ENABLE_OPENCL */

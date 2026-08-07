#include "ttak/ttak_accelerator.h"
#include "ttak/math/ntt.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#ifdef ENABLE_OPENCL
#include <CL/cl.h>

static const char *kNTTOpenCLKernelSrc =
"inline ulong ttak_cl_montgomery_mul(ulong a, ulong b, ulong modulus, ulong mont_inv) {\n"
"    ulong low = a * b;\n"
"    ulong high = mul_hi(a, b);\n"
"    ulong m = low * mont_inv;\n"
"    ulong m_mod_low = m * modulus;\n"
"    ulong m_mod_high = mul_hi(m, modulus);\n"
"    ulong sum_low = low + m_mod_low;\n"
"    ulong carry = (sum_low < low) ? 1UL : 0UL;\n"
"    ulong res = high + m_mod_high + carry;\n"
"    if (res >= modulus) res -= modulus;\n"
"    return res;\n"
"}\n"
"\n"
"__kernel void ttak_ntt_cl_kernel(__global ulong *data, ulong n, ulong modulus, ulong len, ulong twiddle_stride, __global const ulong *twiddle, ulong mont_inv) {\n"
"    size_t idx = get_global_id(0);\n"
"    if (idx >= n / 2) return;\n"
"\n"
"    size_t i = (idx / len) * (len * 2);\n"
"    size_t j = idx % len;\n"
"\n"
"    ulong w = twiddle[j * twiddle_stride];\n"
"\n"
"    size_t u_idx = i + j;\n"
"    size_t v_idx = i + j + len;\n"
"\n"
"    ulong u = data[u_idx];\n"
"    ulong v = data[v_idx];\n"
"\n"
"    ulong t = ttak_cl_montgomery_mul(v, w, modulus, mont_inv);\n"
"\n"
"    ulong add = u + t;\n"
"    if (add >= modulus) add -= modulus;\n"
"\n"
"    ulong sub = (u >= t) ? (u - t) : (u + modulus - t);\n"
"\n"
"    data[u_idx] = add; \n"
"    data[v_idx] = sub;\n"
"}\n";

typedef struct {
    cl_context context;
    cl_command_queue queue;
    cl_program program;
    cl_kernel kernel;
    cl_device_id device;
    bool ready;
} ttak_ntt_cl_context_t;

static ttak_ntt_cl_context_t g_ntt_cl = {0};
static bool g_ntt_cl_probe_failed = false;

static void ttak_ntt_cl_release(void) {
    if (g_ntt_cl.kernel) {
        clReleaseKernel(g_ntt_cl.kernel);
        g_ntt_cl.kernel = NULL;
    }
    if (g_ntt_cl.program) {
        clReleaseProgram(g_ntt_cl.program);
        g_ntt_cl.program = NULL;
    }
    if (g_ntt_cl.queue) {
        clReleaseCommandQueue(g_ntt_cl.queue);
        g_ntt_cl.queue = NULL;
    }
    if (g_ntt_cl.context) {
        clReleaseContext(g_ntt_cl.context);
        g_ntt_cl.context = NULL;
    }
    g_ntt_cl.ready = false;
}

static bool ttak_ntt_cl_build(void) {
    if (g_ntt_cl.ready) return true;
    if (g_ntt_cl_probe_failed) return false;

    cl_int err = CL_SUCCESS;
    cl_uint platform_count = 0;
    if (clGetPlatformIDs(0, NULL, &platform_count) != CL_SUCCESS || platform_count == 0) {
        g_ntt_cl_probe_failed = true;
        return false;
    }

    cl_platform_id platform = NULL;
    if (clGetPlatformIDs(1, &platform, NULL) != CL_SUCCESS) {
        g_ntt_cl_probe_failed = true;
        return false;
    }

    cl_uint device_count = 0;
    err = clGetDeviceIDs(platform, CL_DEVICE_TYPE_GPU, 1, &g_ntt_cl.device, &device_count);
    if (err != CL_SUCCESS) {
        err = clGetDeviceIDs(platform, CL_DEVICE_TYPE_CPU, 1, &g_ntt_cl.device, &device_count);
        if (err != CL_SUCCESS) {
            g_ntt_cl_probe_failed = true;
            return false;
        }
    }

    g_ntt_cl.context = clCreateContext(NULL, 1, &g_ntt_cl.device, NULL, NULL, &err);
    if (err != CL_SUCCESS || g_ntt_cl.context == NULL) {
        ttak_ntt_cl_release();
        g_ntt_cl_probe_failed = true;
        return false;
    }

#if defined(CL_TARGET_OPENCL_VERSION) && (CL_TARGET_OPENCL_VERSION >= 200)
    g_ntt_cl.queue = clCreateCommandQueueWithProperties(g_ntt_cl.context, g_ntt_cl.device, NULL, &err);
#else
    g_ntt_cl.queue = clCreateCommandQueue(g_ntt_cl.context, g_ntt_cl.device, 0, &err);
#endif
    if (err != CL_SUCCESS || g_ntt_cl.queue == NULL) {
        ttak_ntt_cl_release();
        g_ntt_cl_probe_failed = true;
        return false;
    }

    const char *src = kNTTOpenCLKernelSrc;
    size_t len = strlen(kNTTOpenCLKernelSrc);
    g_ntt_cl.program = clCreateProgramWithSource(g_ntt_cl.context, 1, &src, &len, &err);
    if (err != CL_SUCCESS || g_ntt_cl.program == NULL) {
        ttak_ntt_cl_release();
        g_ntt_cl_probe_failed = true;
        return false;
    }

    err = clBuildProgram(g_ntt_cl.program, 1, &g_ntt_cl.device, NULL, NULL, NULL);
    if (err != CL_SUCCESS) {
        ttak_ntt_cl_release();
        g_ntt_cl_probe_failed = true;
        return false;
    }

    g_ntt_cl.kernel = clCreateKernel(g_ntt_cl.program, "ttak_ntt_cl_kernel", &err);
    if (err != CL_SUCCESS || g_ntt_cl.kernel == NULL) {
        ttak_ntt_cl_release();
        g_ntt_cl_probe_failed = true;
        return false;
    }

    g_ntt_cl.ready = true;
    return true;
}

ttak_result_t ttak_accel_ntt_opencl(
    uint64_t *data,
    size_t n,
    const ttak_ntt_prime_t *prime,
    bool inverse) {
    if (!data || !prime || n == 0) return TTAK_RESULT_ERR_ARGUMENT;

    if (!ttak_ntt_cl_build()) {
        return TTAK_RESULT_ERR_EXECUTION;
    }

    cl_int err = CL_SUCCESS;
    cl_mem data_buf = clCreateBuffer(g_ntt_cl.context,
                                     CL_MEM_READ_WRITE | CL_MEM_COPY_HOST_PTR,
                                     sizeof(uint64_t) * n,
                                     data,
                                     &err);
    if (err != CL_SUCCESS || data_buf == NULL) {
        return TTAK_RESULT_ERR_EXECUTION;
    }

    uint64_t *twiddle = (uint64_t *)malloc(sizeof(uint64_t) * n);
    if (!twiddle) {
        clReleaseMemObject(data_buf);
        return TTAK_RESULT_ERR_EXECUTION;
    }

    uint64_t modulus = prime->modulus;
    uint64_t root = ttak_mod_pow(prime->primitive_root, (prime->modulus - 1) / n, modulus);
    if (inverse) {
        root = ttak_mod_inverse(root, modulus);
    }
    uint64_t root_mont = ttak_montgomery_convert(root, prime);
    uint64_t curr = ttak_montgomery_convert(1ULL, prime);
    for (size_t i = 0; i < n; ++i) {
        twiddle[i] = curr;
        curr = ttak_montgomery_mul(curr, root_mont, prime);
    }

    cl_mem twiddle_buf = clCreateBuffer(g_ntt_cl.context,
                                        CL_MEM_READ_ONLY | CL_MEM_COPY_HOST_PTR,
                                        sizeof(uint64_t) * n,
                                        twiddle,
                                        &err);
    free(twiddle);
    if (err != CL_SUCCESS || twiddle_buf == NULL) {
        clReleaseMemObject(data_buf);
        return TTAK_RESULT_ERR_EXECUTION;
    }

    size_t local = 256;
    size_t global = ((n / 2 + local - 1) / local) * local;

    for (size_t len = 1; len < n; len <<= 1) {
        size_t step = len << 1;
        cl_ulong twiddle_stride = (cl_ulong)(n / step);
        cl_ulong cl_n = (cl_ulong)n;
        cl_ulong cl_modulus = (cl_ulong)modulus;
        cl_ulong cl_len = (cl_ulong)len;
        cl_ulong cl_mont_inv = (cl_ulong)(prime->montgomery_inv);

        err = clSetKernelArg(g_ntt_cl.kernel, 0, sizeof(cl_mem), &data_buf);
        err |= clSetKernelArg(g_ntt_cl.kernel, 1, sizeof(cl_ulong), &cl_n);
        err |= clSetKernelArg(g_ntt_cl.kernel, 2, sizeof(cl_ulong), &cl_modulus);
        err |= clSetKernelArg(g_ntt_cl.kernel, 3, sizeof(cl_ulong), &cl_len);
        err |= clSetKernelArg(g_ntt_cl.kernel, 4, sizeof(cl_ulong), &twiddle_stride);
        err |= clSetKernelArg(g_ntt_cl.kernel, 5, sizeof(cl_mem), &twiddle_buf);
        err |= clSetKernelArg(g_ntt_cl.kernel, 6, sizeof(cl_ulong), &cl_mont_inv);

        if (err != CL_SUCCESS) {
            clReleaseMemObject(twiddle_buf);
            clReleaseMemObject(data_buf);
            return TTAK_RESULT_ERR_EXECUTION;
        }

        err = clEnqueueNDRangeKernel(g_ntt_cl.queue, g_ntt_cl.kernel, 1, NULL, &global, &local, 0, NULL, NULL);
        if (err != CL_SUCCESS) {
            clReleaseMemObject(twiddle_buf);
            clReleaseMemObject(data_buf);
            return TTAK_RESULT_ERR_EXECUTION;
        }
    }

    err = clFinish(g_ntt_cl.queue);
    if (err != CL_SUCCESS) {
        clReleaseMemObject(twiddle_buf);
        clReleaseMemObject(data_buf);
        return TTAK_RESULT_ERR_EXECUTION;
    }

    err = clEnqueueReadBuffer(g_ntt_cl.queue,
                              data_buf,
                              CL_TRUE,
                              0,
                              sizeof(uint64_t) * n,
                              data,
                              0,
                              NULL,
                              NULL);

    clReleaseMemObject(twiddle_buf);
    clReleaseMemObject(data_buf);

    if (err != CL_SUCCESS) {
        return TTAK_RESULT_ERR_EXECUTION;
    }

    return TTAK_RESULT_OK;
}
#else
ttak_result_t ttak_accel_ntt_opencl(
    uint64_t *data,
    size_t n,
    const ttak_ntt_prime_t *prime,
    bool inverse) {
    (void)data; (void)n; (void)prime; (void)inverse;
    return TTAK_RESULT_ERR_UNSUPPORTED;
}
#endif

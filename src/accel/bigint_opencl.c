#include <ttak/math/bigint.h>

#ifdef ENABLE_OPENCL

#include <CL/cl.h>

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
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
"                         __global uint *out, uint out_len) {\n"
"    if (get_global_id(0) != 0) return;\n"
"    if (out_len == 0) return;\n"
"    ulong carry = 0ul;\n"
"    uint limit = (out_len > 0) ? (out_len - 1u) : 0u;\n"
"    for (uint i = 0; i < limit; ++i) {\n"
"        ulong sum = carry;\n"
"        if (i < lhs_len) sum += (ulong)lhs[i];\n"
"        if (i < rhs_len) sum += (ulong)rhs[i];\n"
"        out[i] = (uint)(sum & 0xFFFFFFFFul);\n"
"        carry = sum >> 32;\n"
"    }\n"
"    out[limit] = (uint)carry;\n"
"}\n"
"\n"
"__kernel void bigint_mul(__global const uint *lhs, uint lhs_len,\n"
"                         __global const uint *rhs, uint rhs_len,\n"
"                         __global uint *out, uint out_len) {\n"
"    if (get_global_id(0) != 0) return;\n"
"    if (lhs_len == 0 || rhs_len == 0 || out_len == 0) return;\n"
"    for (uint n = 0; n < out_len; ++n) {\n"
"        out[n] = 0u;\n"
"    }\n"
"    for (uint i = 0; i < lhs_len; ++i) {\n"
"        ulong carry = 0ul;\n"
"        for (uint j = 0; j < rhs_len; ++j) {\n"
"            uint idx = i + j;\n"
"            if (idx >= out_len) continue;\n"
"            ulong sum = (ulong)out[idx] + ((ulong)lhs[i] * (ulong)rhs[j]) + carry;\n"
"            out[idx] = (uint)(sum & 0xFFFFFFFFul);\n"
"            carry = sum >> 32;\n"
"        }\n"
"        uint k = i + rhs_len;\n"
"        while (carry > 0ul && k < out_len) {\n"
"            ulong sum = (ulong)out[k] + carry;\n"
"            out[k] = (uint)(sum & 0xFFFFFFFFul);\n"
"            carry = sum >> 32;\n"
"            ++k;\n"
"        }\n"
"    }\n"
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
    cl_mem out_buf = clCreateBuffer(g_bigint_ocl.context, CL_MEM_WRITE_ONLY, result_len * sizeof(cl_uint), NULL, &err);
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
    err |= clSetKernelArg(kernel, 4, sizeof(cl_mem), &out_buf);
    err |= clSetKernelArg(kernel, 5, sizeof(cl_uint), &(cl_uint){ (cl_uint)result_len });
    if (err != CL_SUCCESS) {
        clReleaseMemObject(lhs_buf);
        clReleaseMemObject(rhs_buf);
        clReleaseMemObject(out_buf);
        return false;
    }

    size_t global = 1;
    err = clEnqueueNDRangeKernel(g_bigint_ocl.queue, kernel, 1, NULL, &global, NULL, 0, NULL, NULL);
    if (err == CL_SUCCESS) {
        err = clFinish(g_bigint_ocl.queue);
    }

    bool ok = (err == CL_SUCCESS);
    if (ok) {
        err = clEnqueueReadBuffer(g_bigint_ocl.queue, out_buf, CL_TRUE, 0,
                                  result_len * sizeof(cl_uint), dst, 0, NULL, NULL);
        ok = (err == CL_SUCCESS);
    }

    clReleaseMemObject(lhs_buf);
    clReleaseMemObject(rhs_buf);
    clReleaseMemObject(out_buf);

    if (!ok) return false;

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
    cl_mem out_buf = clCreateBuffer(g_bigint_ocl.context, CL_MEM_READ_WRITE, result_len * sizeof(cl_uint), NULL, &err);
    if (err != CL_SUCCESS) {
        clReleaseMemObject(lhs_buf);
        clReleaseMemObject(rhs_buf);
        return false;
    }

    cl_kernel kernel = g_bigint_ocl.mul_kernel;
    err  = clSetKernelArg(kernel, 0, sizeof(cl_mem), &lhs_buf);
    err |= clSetKernelArg(kernel, 1, sizeof(cl_uint), &(cl_uint){ (cl_uint)lhs_used });
    err |= clSetKernelArg(kernel, 2, sizeof(cl_mem), &rhs_buf);
    err |= clSetKernelArg(kernel, 3, sizeof(cl_uint), &(cl_uint){ (cl_uint)rhs_used });
    err |= clSetKernelArg(kernel, 4, sizeof(cl_mem), &out_buf);
    err |= clSetKernelArg(kernel, 5, sizeof(cl_uint), &(cl_uint){ (cl_uint)result_len });
    if (err != CL_SUCCESS) {
        clReleaseMemObject(lhs_buf);
        clReleaseMemObject(rhs_buf);
        clReleaseMemObject(out_buf);
        return false;
    }

    size_t global = 1;
    err = clEnqueueNDRangeKernel(g_bigint_ocl.queue, kernel, 1, NULL, &global, NULL, 0, NULL, NULL);
    if (err == CL_SUCCESS) {
        err = clFinish(g_bigint_ocl.queue);
    }

    bool ok = (err == CL_SUCCESS);
    if (ok) {
        err = clEnqueueReadBuffer(g_bigint_ocl.queue, out_buf, CL_TRUE, 0,
                                  result_len * sizeof(cl_uint), dst, 0, NULL, NULL);
        ok = (err == CL_SUCCESS);
    }

    clReleaseMemObject(lhs_buf);
    clReleaseMemObject(rhs_buf);
    clReleaseMemObject(out_buf);

    if (!ok) return false;

    ttak_bigint_opencl_trim(dst, result_len, out_used);
    return true;
}

#endif /* ENABLE_OPENCL */

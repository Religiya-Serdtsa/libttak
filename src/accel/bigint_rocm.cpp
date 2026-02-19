#include <ttak/math/bigint.h>

#include <hip/hip_runtime.h>

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

__global__ static void ttak_bigint_hip_add_kernel(const limb_t *lhs,
                                                  uint32_t lhs_len,
                                                  const limb_t *rhs,
                                                  uint32_t rhs_len,
                                                  limb_t *out,
                                                  uint32_t out_len) {
    if (blockIdx.x != 0 || threadIdx.x != 0) return;
    if (out_len == 0) return;

    uint64_t carry = 0;
    uint32_t main_len = out_len > 0 ? (out_len - 1u) : 0u;
    for (uint32_t i = 0; i < main_len; ++i) {
        uint64_t sum = carry;
        if (lhs && i < lhs_len) sum += lhs[i];
        if (rhs && i < rhs_len) sum += rhs[i];
        out[i] = (limb_t)sum;
        carry = sum >> 32;
    }
    out[main_len] = (limb_t)carry;
}

__global__ static void ttak_bigint_hip_mul_kernel(const limb_t *lhs,
                                                  uint32_t lhs_len,
                                                  const limb_t *rhs,
                                                  uint32_t rhs_len,
                                                  limb_t *out,
                                                  uint32_t out_len) {
    if (blockIdx.x != 0 || threadIdx.x != 0) return;
    if (!lhs || !rhs || !out) return;
    if (lhs_len == 0 || rhs_len == 0 || out_len == 0) return;

    for (uint32_t i = 0; i < lhs_len; ++i) {
        uint64_t carry = 0;
        for (uint32_t j = 0; j < rhs_len; ++j) {
            uint32_t idx = i + j;
            if (idx >= out_len) continue;
            uint64_t existing = out[idx];
            uint64_t prod = (uint64_t)lhs[i] * (uint64_t)rhs[j];
            uint64_t sum = existing + prod + carry;
            out[idx] = (limb_t)sum;
            carry = sum >> 32;
        }
        uint32_t k = i + rhs_len;
        while (carry > 0 && k < out_len) {
            uint64_t sum = (uint64_t)out[k] + carry;
            out[k] = (limb_t)sum;
            carry = sum >> 32;
            ++k;
        }
    }
}

static bool ttak_bigint_hip_copy_to_device(const limb_t *src,
                                           size_t count,
                                           limb_t **device_ptr) {
    if (count == 0 || !src) {
        *device_ptr = NULL;
        return true;
    }
    limb_t *dev = NULL;
    hipError_t err = hipMalloc((void **)&dev, count * sizeof(limb_t));
    if (err != hipSuccess) {
        return false;
    }
    err = hipMemcpy(dev, src, count * sizeof(limb_t), hipMemcpyHostToDevice);
    if (err != hipSuccess) {
        hipFree(dev);
        return false;
    }
    *device_ptr = dev;
    return true;
}

static void ttak_bigint_hip_free_all(limb_t *lhs,
                                     limb_t *rhs,
                                     limb_t *dst) {
    if (lhs) hipFree(lhs);
    if (rhs) hipFree(rhs);
    if (dst) hipFree(dst);
}

extern "C" bool ttak_bigint_accel_rocm_add(limb_t *dst,
                                           size_t dst_capacity,
                                           size_t *out_used,
                                           const limb_t *lhs,
                                           size_t lhs_used,
                                           const limb_t *rhs,
                                           size_t rhs_used) {
    size_t max_used = lhs_used > rhs_used ? lhs_used : rhs_used;
    size_t result_len = max_used + 1;
    if (dst_capacity < result_len) {
        return false;
    }

    limb_t *d_lhs = NULL;
    limb_t *d_rhs = NULL;
    limb_t *d_out = NULL;

    if (!ttak_bigint_hip_copy_to_device(lhs, lhs_used, &d_lhs)) {
        return false;
    }
    if (!ttak_bigint_hip_copy_to_device(rhs, rhs_used, &d_rhs)) {
        ttak_bigint_hip_free_all(d_lhs, NULL, NULL);
        return false;
    }

    hipError_t err = hipMalloc((void **)&d_out, result_len * sizeof(limb_t));
    if (err != hipSuccess) {
        ttak_bigint_hip_free_all(d_lhs, d_rhs, NULL);
        return false;
    }
    err = hipMemset(d_out, 0, result_len * sizeof(limb_t));
    if (err != hipSuccess) {
        ttak_bigint_hip_free_all(d_lhs, d_rhs, d_out);
        return false;
    }

    hipLaunchKernelGGL(ttak_bigint_hip_add_kernel, dim3(1), dim3(1), 0, 0,
                       d_lhs,
                       (uint32_t)lhs_used,
                       d_rhs,
                       (uint32_t)rhs_used,
                       d_out,
                       (uint32_t)result_len);
    err = hipDeviceSynchronize();
    if (err != hipSuccess) {
        ttak_bigint_hip_free_all(d_lhs, d_rhs, d_out);
        return false;
    }

    err = hipMemcpy(dst, d_out, result_len * sizeof(limb_t), hipMemcpyDeviceToHost);
    ttak_bigint_hip_free_all(d_lhs, d_rhs, d_out);
    if (err != hipSuccess) {
        return false;
    }

    size_t used = result_len;
    while (used > 0 && dst[used - 1] == 0) {
        --used;
    }
    if (out_used) {
        *out_used = used;
    }
    return true;
}

extern "C" bool ttak_bigint_accel_rocm_mul(limb_t *dst,
                                           size_t dst_capacity,
                                           size_t *out_used,
                                           const limb_t *lhs,
                                           size_t lhs_used,
                                           const limb_t *rhs,
                                           size_t rhs_used) {
    size_t result_len = lhs_used + rhs_used;
    if (result_len == 0 || dst_capacity < result_len) {
        return false;
    }

    limb_t *d_lhs = NULL;
    limb_t *d_rhs = NULL;
    limb_t *d_out = NULL;

    if (!ttak_bigint_hip_copy_to_device(lhs, lhs_used, &d_lhs)) {
        return false;
    }
    if (!ttak_bigint_hip_copy_to_device(rhs, rhs_used, &d_rhs)) {
        ttak_bigint_hip_free_all(d_lhs, NULL, NULL);
        return false;
    }
    hipError_t err = hipMalloc((void **)&d_out, result_len * sizeof(limb_t));
    if (err != hipSuccess) {
        ttak_bigint_hip_free_all(d_lhs, d_rhs, NULL);
        return false;
    }
    err = hipMemset(d_out, 0, result_len * sizeof(limb_t));
    if (err != hipSuccess) {
        ttak_bigint_hip_free_all(d_lhs, d_rhs, d_out);
        return false;
    }

    hipLaunchKernelGGL(ttak_bigint_hip_mul_kernel, dim3(1), dim3(1), 0, 0,
                       d_lhs,
                       (uint32_t)lhs_used,
                       d_rhs,
                       (uint32_t)rhs_used,
                       d_out,
                       (uint32_t)result_len);
    err = hipDeviceSynchronize();
    if (err != hipSuccess) {
        ttak_bigint_hip_free_all(d_lhs, d_rhs, d_out);
        return false;
    }

    err = hipMemcpy(dst, d_out, result_len * sizeof(limb_t), hipMemcpyDeviceToHost);
    ttak_bigint_hip_free_all(d_lhs, d_rhs, d_out);
    if (err != hipSuccess) {
        return false;
    }

    size_t used = result_len;
    while (used > 0 && dst[used - 1] == 0) {
        --used;
    }
    if (out_used) {
        *out_used = used;
    }
    return true;
}

#include <ttak/math/bigint.h>

#include <cuda_runtime.h>

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

__global__ static void ttak_bigint_cuda_add_kernel(const limb_t *lhs,
                                                   uint32_t lhs_len,
                                                   const limb_t *rhs,
                                                   uint32_t rhs_len,
                                                   uint64_t *temp,
                                                   uint32_t temp_len) {
    uint32_t idx = blockIdx.x * blockDim.x + threadIdx.x;
    for (uint32_t i = idx; i < temp_len; i += blockDim.x * gridDim.x) {
        uint64_t sum = 0;
        if (lhs && i < lhs_len) sum += lhs[i];
        if (rhs && i < rhs_len) sum += rhs[i];
        temp[i] = sum;
    }
}

__global__ static void ttak_bigint_cuda_mul_kernel(const limb_t *lhs,
                                                   uint32_t lhs_len,
                                                   const limb_t *rhs,
                                                   uint32_t rhs_len,
                                                   unsigned __int128 *temp,
                                                   uint32_t temp_len) {
    uint32_t k = blockIdx.x * blockDim.x + threadIdx.x;
    if (k >= temp_len) return;

    unsigned __int128 sum = 0;
    for (uint32_t i = 0; i < lhs_len && i <= k; ++i) {
        uint32_t j = k - i;
        if (j >= rhs_len) continue;
        sum += (unsigned __int128)((uint64_t)lhs[i] * (uint64_t)rhs[j]);
    }
    temp[k] = sum;
}

static bool ttak_bigint_cuda_copy_to_device(const limb_t *src,
                                            size_t count,
                                            limb_t **device_ptr) {
    if (count == 0 || !src) {
        *device_ptr = NULL;
        return true;
    }
    limb_t *dev = NULL;
    cudaError_t err = cudaMalloc((void **)&dev, count * sizeof(limb_t));
    if (err != cudaSuccess) {
        return false;
    }
    err = cudaMemcpy(dev, src, count * sizeof(limb_t), cudaMemcpyHostToDevice);
    if (err != cudaSuccess) {
        cudaFree(dev);
        return false;
    }
    *device_ptr = dev;
    return true;
}

static void ttak_bigint_cuda_free_all(limb_t *lhs,
                                      limb_t *rhs,
                                      void *tmp) {
    if (lhs) cudaFree(lhs);
    if (rhs) cudaFree(rhs);
    if (tmp) cudaFree(tmp);
}

extern "C" bool ttak_bigint_accel_cuda_add(limb_t *dst,
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
    uint64_t *d_temp = NULL;

    if (!ttak_bigint_cuda_copy_to_device(lhs, lhs_used, &d_lhs)) {
        return false;
    }
    if (!ttak_bigint_cuda_copy_to_device(rhs, rhs_used, &d_rhs)) {
        ttak_bigint_cuda_free_all(d_lhs, NULL, NULL);
        return false;
    }

    cudaError_t err = cudaMalloc((void **)&d_temp, result_len * sizeof(uint64_t));
    if (err != cudaSuccess) {
        ttak_bigint_cuda_free_all(d_lhs, d_rhs, NULL);
        return false;
    }
    err = cudaMemset(d_temp, 0, result_len * sizeof(uint64_t));
    if (err != cudaSuccess) {
        ttak_bigint_cuda_free_all(d_lhs, d_rhs, d_temp);
        return false;
    }

    int threads = 256;
    int blocks = (int)((result_len + threads - 1) / threads);
    ttak_bigint_cuda_add_kernel<<<blocks, threads>>>(
        d_lhs, (uint32_t)lhs_used, d_rhs, (uint32_t)rhs_used,
        d_temp, (uint32_t)result_len);
    err = cudaDeviceSynchronize();
    if (err != cudaSuccess) {
        ttak_bigint_cuda_free_all(d_lhs, d_rhs, d_temp);
        return false;
    }

    uint64_t *temp = (uint64_t *)malloc(result_len * sizeof(uint64_t));
    if (!temp) {
        ttak_bigint_cuda_free_all(d_lhs, d_rhs, d_temp);
        return false;
    }
    err = cudaMemcpy(temp, d_temp, result_len * sizeof(uint64_t), cudaMemcpyDeviceToHost);
    ttak_bigint_cuda_free_all(d_lhs, d_rhs, d_temp);
    if (err != cudaSuccess) {
        free(temp);
        return false;
    }

    uint64_t carry = 0;
    for (size_t i = 0; i < result_len; ++i) {
        uint64_t v = temp[i] + carry;
        dst[i] = (limb_t)v;
        carry = v >> 32;
    }
    free(temp);

    size_t used = result_len;
    while (used > 0 && dst[used - 1] == 0) {
        --used;
    }
    if (out_used) {
        *out_used = used;
    }
    return true;
}

extern "C" bool ttak_bigint_accel_cuda_mul(limb_t *dst,
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
    unsigned __int128 *d_temp = NULL;

    if (!ttak_bigint_cuda_copy_to_device(lhs, lhs_used, &d_lhs)) {
        return false;
    }
    if (!ttak_bigint_cuda_copy_to_device(rhs, rhs_used, &d_rhs)) {
        ttak_bigint_cuda_free_all(d_lhs, NULL, NULL);
        return false;
    }
    cudaError_t err = cudaMalloc((void **)&d_temp, result_len * sizeof(unsigned __int128));
    if (err != cudaSuccess) {
        ttak_bigint_cuda_free_all(d_lhs, d_rhs, NULL);
        return false;
    }
    err = cudaMemset(d_temp, 0, result_len * sizeof(unsigned __int128));
    if (err != cudaSuccess) {
        ttak_bigint_cuda_free_all(d_lhs, d_rhs, d_temp);
        return false;
    }

    int threads = 256;
    int blocks = (int)((result_len + threads - 1) / threads);
    ttak_bigint_cuda_mul_kernel<<<blocks, threads>>>(
        d_lhs, (uint32_t)lhs_used, d_rhs, (uint32_t)rhs_used,
        d_temp, (uint32_t)result_len);
    err = cudaDeviceSynchronize();
    if (err != cudaSuccess) {
        ttak_bigint_cuda_free_all(d_lhs, d_rhs, d_temp);
        return false;
    }

    unsigned __int128 *temp = (unsigned __int128 *)malloc(result_len * sizeof(unsigned __int128));
    if (!temp) {
        ttak_bigint_cuda_free_all(d_lhs, d_rhs, d_temp);
        return false;
    }
    err = cudaMemcpy(temp, d_temp, result_len * sizeof(unsigned __int128), cudaMemcpyDeviceToHost);
    ttak_bigint_cuda_free_all(d_lhs, d_rhs, d_temp);
    if (err != cudaSuccess) {
        free(temp);
        return false;
    }

    uint64_t carry = 0;
    for (size_t i = 0; i < result_len; ++i) {
        unsigned __int128 v = temp[i] + carry;
        dst[i] = (limb_t)(uint64_t)(v & 0xFFFFFFFF);
        carry = (uint64_t)(v >> 32);
    }
    free(temp);

    size_t used = result_len;
    while (used > 0 && dst[used - 1] == 0) {
        --used;
    }
    if (out_used) {
        *out_used = used;
    }
    return true;
}

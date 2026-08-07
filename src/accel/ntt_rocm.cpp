#include "ttak/ttak_accelerator.h"
#include "ttak/math/ntt.h"
#include <hip/hip_runtime.h>
#include <stdint.h>
#include <stdlib.h>

__device__ __forceinline__ uint64_t ttak_rocm_umul64hi(uint64_t a, uint64_t b) {
#if defined(__HIP_DEVICE_COMPILE__) && defined(__HIP_PLATFORM_AMD__)
    return __umul64hi(a, b);
#elif defined(__SIZEOF_INT128__)
    return (uint64_t)(((unsigned __int128)a * b) >> 64);
#else
    uint64_t a_lo = (uint32_t)a, a_hi = a >> 32;
    uint64_t b_lo = (uint32_t)b, b_hi = b >> 32;
    uint64_t p0 = a_lo * b_lo;
    uint64_t p1 = a_lo * b_hi;
    uint64_t p2 = a_hi * b_lo;
    uint64_t p3 = a_hi * b_hi;
    uint64_t cy0 = (p0 >> 32) + (uint32_t)p1 + (uint32_t)p2;
    return p3 + (p1 >> 32) + (p2 >> 32) + (cy0 >> 32);
#endif
}

__device__ __forceinline__ uint64_t ttak_rocm_mont_mul(uint64_t a, uint64_t b, uint64_t modulus, uint64_t mont_inv) {
    uint64_t low = a * b;
    uint64_t high = ttak_rocm_umul64hi(a, b);
    uint64_t m = low * mont_inv;
    uint64_t m_mod_low = m * modulus;
    uint64_t m_mod_high = ttak_rocm_umul64hi(m, modulus);
    uint64_t sum_low = low + m_mod_low;
    uint64_t carry = (sum_low < low) ? 1ULL : 0ULL;
    uint64_t res = high + m_mod_high + carry;
    if (res >= modulus) res -= modulus;
    return res;
}

__global__ void ttak_ntt_rocm_kernel(uint64_t *data, size_t n, uint64_t modulus, uint64_t len, uint64_t twiddle_stride, const uint64_t *twiddle, uint64_t mont_inv) {
    size_t idx = blockIdx.x * blockDim.x + threadIdx.x;
    if (idx >= n / 2) return;

    size_t i = (idx / len) * (len * 2);
    size_t j = idx % len;

    uint64_t w = twiddle[j * twiddle_stride];

    size_t u_idx = i + j;
    size_t v_idx = i + j + len;

    uint64_t u = data[u_idx];
    uint64_t v = data[v_idx];

    uint64_t t = ttak_rocm_mont_mul(v, w, modulus, mont_inv);

    uint64_t add = u + t;
    if (add >= modulus) add -= modulus;

    uint64_t sub = (u >= t) ? (u - t) : (u + modulus - t);

    data[u_idx] = add;
    data[v_idx] = sub;
}

extern "C" ttak_result_t ttak_accel_ntt_rocm(
    uint64_t *data,
    size_t n,
    const ttak_ntt_prime_t *prime,
    bool inverse) {
    if (!data || !prime || n == 0) return TTAK_RESULT_ERR_ARGUMENT;

    uint64_t *data_dev = NULL;
    uint64_t *twiddle_dev = NULL;

    if (hipMalloc((void **)&data_dev, sizeof(uint64_t) * n) != hipSuccess) {
        return TTAK_RESULT_ERR_EXECUTION;
    }

    uint64_t *twiddle = (uint64_t *)malloc(sizeof(uint64_t) * n);
    if (!twiddle) {
        hipFree(data_dev);
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

    if (hipMalloc((void **)&twiddle_dev, sizeof(uint64_t) * n) != hipSuccess) {
        free(twiddle);
        hipFree(data_dev);
        return TTAK_RESULT_ERR_EXECUTION;
    }

    if (hipMemcpy(data_dev, data, sizeof(uint64_t) * n, hipMemcpyHostToDevice) != hipSuccess ||
        hipMemcpy(twiddle_dev, twiddle, sizeof(uint64_t) * n, hipMemcpyHostToDevice) != hipSuccess) {
        free(twiddle);
        hipFree(twiddle_dev);
        hipFree(data_dev);
        return TTAK_RESULT_ERR_EXECUTION;
    }

    size_t threads = 256;
    size_t num_elements = n / 2;
    size_t blocks = (num_elements + threads - 1) / threads;

    for (size_t len = 1; len < n; len <<= 1) {
        size_t step = len << 1;
        size_t twiddle_stride = n / step;
        hipLaunchKernelGGL(ttak_ntt_rocm_kernel, dim3(blocks), dim3(threads), 0, 0, data_dev, n, modulus, len, twiddle_stride, twiddle_dev, prime->montgomery_inv);
    }
    hipDeviceSynchronize();

    if (hipMemcpy(data, data_dev, sizeof(uint64_t) * n, hipMemcpyDeviceToHost) != hipSuccess) {
        free(twiddle);
        hipFree(twiddle_dev);
        hipFree(data_dev);
        return TTAK_RESULT_ERR_EXECUTION;
    }

    free(twiddle);
    hipFree(twiddle_dev);
    hipFree(data_dev);
    return TTAK_RESULT_OK;
}

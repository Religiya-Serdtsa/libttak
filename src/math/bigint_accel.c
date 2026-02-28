#include <ttak/math/bigint_accel.h>

#include <stdatomic.h>
#include <stdlib.h>

static size_t g_bigint_accel_threshold = 0;
static bool g_bigint_accel_threshold_ready = false;

#if defined(ENABLE_CUDA)
static atomic_bool g_bigint_accel_cuda_enabled = ATOMIC_VAR_INIT(true);
#endif

#if defined(ENABLE_ROCM)
static atomic_bool g_bigint_accel_rocm_enabled = ATOMIC_VAR_INIT(true);
#endif

#if defined(ENABLE_OPENCL)
static atomic_bool g_bigint_accel_opencl_enabled = ATOMIC_VAR_INIT(true);
#endif

static void ttak_bigint_accel_init_threshold(void) {
    if (g_bigint_accel_threshold_ready) return;
    g_bigint_accel_threshold = 256;
    const char *env = getenv("TTAK_BIGINT_ACCEL_THRESHOLD");
    if (env && *env) {
        char *endp = NULL;
        long parsed = strtol(env, &endp, 10);
        if (endp && *endp == '\0' && parsed > 0) {
            g_bigint_accel_threshold = (size_t)parsed;
        }
    }
    g_bigint_accel_threshold_ready = true;
}

size_t ttak_bigint_accel_min_limbs(void) {
    ttak_bigint_accel_init_threshold();
    return g_bigint_accel_threshold;
}

bool ttak_bigint_accel_available(void) {
    bool available = false;
#if defined(ENABLE_CUDA)
    if (atomic_load_explicit(&g_bigint_accel_cuda_enabled, memory_order_relaxed)) {
        available = true;
    }
#endif
#if defined(ENABLE_ROCM)
    if (atomic_load_explicit(&g_bigint_accel_rocm_enabled, memory_order_relaxed)) {
        available = true;
    }
#endif
#if defined(ENABLE_OPENCL)
    if (atomic_load_explicit(&g_bigint_accel_opencl_enabled, memory_order_relaxed)) {
        available = true;
    }
#endif
    return available;
}

#if defined(ENABLE_CUDA)
bool ttak_bigint_accel_cuda_add(limb_t *dst,
                                size_t dst_capacity,
                                size_t *out_used,
                                const limb_t *lhs,
                                size_t lhs_used,
                                const limb_t *rhs,
                                size_t rhs_used);
bool ttak_bigint_accel_cuda_mul(limb_t *dst,
                                size_t dst_capacity,
                                size_t *out_used,
                                const limb_t *lhs,
                                size_t lhs_used,
                                const limb_t *rhs,
                                size_t rhs_used);
#endif

#if defined(ENABLE_ROCM)
bool ttak_bigint_accel_rocm_add(limb_t *dst,
                                size_t dst_capacity,
                                size_t *out_used,
                                const limb_t *lhs,
                                size_t lhs_used,
                                const limb_t *rhs,
                                size_t rhs_used);
bool ttak_bigint_accel_rocm_mul(limb_t *dst,
                                size_t dst_capacity,
                                size_t *out_used,
                                const limb_t *lhs,
                                size_t lhs_used,
                                const limb_t *rhs,
                                size_t rhs_used);
#endif

#if defined(ENABLE_OPENCL)
bool ttak_bigint_accel_opencl_add(limb_t *dst,
                                  size_t dst_capacity,
                                  size_t *out_used,
                                  const limb_t *lhs,
                                  size_t lhs_used,
                                  const limb_t *rhs,
                                  size_t rhs_used);
bool ttak_bigint_accel_opencl_mul(limb_t *dst,
                                  size_t dst_capacity,
                                  size_t *out_used,
                                  const limb_t *lhs,
                                  size_t lhs_used,
                                  const limb_t *rhs,
                                  size_t rhs_used);
#endif

bool ttak_bigint_accel_add_raw(limb_t *dst,
                               size_t dst_capacity,
                               size_t *out_used,
                               const limb_t *lhs,
                               size_t lhs_used,
                               const limb_t *rhs,
                               size_t rhs_used) {
    (void)dst;
    (void)dst_capacity;
    (void)out_used;
    (void)lhs;
    (void)lhs_used;
    (void)rhs;
    (void)rhs_used;
#if defined(ENABLE_CUDA)
    if (atomic_load_explicit(&g_bigint_accel_cuda_enabled, memory_order_relaxed)) {
        if (ttak_bigint_accel_cuda_add(dst, dst_capacity, out_used, lhs, lhs_used, rhs, rhs_used)) {
            return true;
        }
        atomic_store_explicit(&g_bigint_accel_cuda_enabled, false, memory_order_relaxed);
    }
#endif
#if defined(ENABLE_ROCM)
    if (atomic_load_explicit(&g_bigint_accel_rocm_enabled, memory_order_relaxed)) {
        if (ttak_bigint_accel_rocm_add(dst, dst_capacity, out_used, lhs, lhs_used, rhs, rhs_used)) {
            return true;
        }
        atomic_store_explicit(&g_bigint_accel_rocm_enabled, false, memory_order_relaxed);
    }
#endif
#if defined(ENABLE_OPENCL)
    if (atomic_load_explicit(&g_bigint_accel_opencl_enabled, memory_order_relaxed)) {
        if (ttak_bigint_accel_opencl_add(dst, dst_capacity, out_used, lhs, lhs_used, rhs, rhs_used)) {
            return true;
        }
        atomic_store_explicit(&g_bigint_accel_opencl_enabled, false, memory_order_relaxed);
    }
#endif
    return false;
}

bool ttak_bigint_accel_mul_raw(limb_t *dst,
                               size_t dst_capacity,
                               size_t *out_used,
                               const limb_t *lhs,
                               size_t lhs_used,
                               const limb_t *rhs,
                               size_t rhs_used) {
    (void)dst;
    (void)dst_capacity;
    (void)out_used;
    (void)lhs;
    (void)lhs_used;
    (void)rhs;
    (void)rhs_used;
#if defined(ENABLE_CUDA)
    if (atomic_load_explicit(&g_bigint_accel_cuda_enabled, memory_order_relaxed)) {
        if (ttak_bigint_accel_cuda_mul(dst, dst_capacity, out_used, lhs, lhs_used, rhs, rhs_used)) {
            return true;
        }
        atomic_store_explicit(&g_bigint_accel_cuda_enabled, false, memory_order_relaxed);
    }
#endif
#if defined(ENABLE_ROCM)
    if (atomic_load_explicit(&g_bigint_accel_rocm_enabled, memory_order_relaxed)) {
        if (ttak_bigint_accel_rocm_mul(dst, dst_capacity, out_used, lhs, lhs_used, rhs, rhs_used)) {
            return true;
        }
        atomic_store_explicit(&g_bigint_accel_rocm_enabled, false, memory_order_relaxed);
    }
#endif
#if defined(ENABLE_OPENCL)
    if (atomic_load_explicit(&g_bigint_accel_opencl_enabled, memory_order_relaxed)) {
        if (ttak_bigint_accel_opencl_mul(dst, dst_capacity, out_used, lhs, lhs_used, rhs, rhs_used)) {
            return true;
        }
        atomic_store_explicit(&g_bigint_accel_opencl_enabled, false, memory_order_relaxed);
    }
#endif
    return false;
}

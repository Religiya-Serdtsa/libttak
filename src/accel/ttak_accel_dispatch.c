#include "ttak/ttak_accelerator.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdatomic.h>

static _Atomic uintptr_t g_backend_ptr = ATOMIC_VAR_INIT((uintptr_t)0);
static _Atomic ttak_accel_target_t g_backend_target =
    ATOMIC_VAR_INIT(TTAK_ACCEL_TARGET_CPU);

/* Forward declarations for each backend. */
ttak_result_t ttak_accel_run_cpu(
    const ttak_accel_batch_item_t *items,
    size_t item_count,
    const ttak_accel_config_t *config);

#ifdef ENABLE_CUDA
ttak_result_t ttak_accel_run_cuda(
    const ttak_accel_batch_item_t *items,
    size_t item_count,
    const ttak_accel_config_t *config);
#endif

#ifdef ENABLE_OPENCL
ttak_result_t ttak_accel_run_opencl(
    const ttak_accel_batch_item_t *items,
    size_t item_count,
    const ttak_accel_config_t *config);
#endif

#ifdef ENABLE_ROCM
ttak_result_t ttak_accel_run_rocm(
    const ttak_accel_batch_item_t *items,
    size_t item_count,
    const ttak_accel_config_t *config);
#endif

static inline ttak_accel_backend_fn ttak_backend_for_target(
    ttak_accel_target_t target) {
    switch (target) {
        case TTAK_ACCEL_TARGET_CPU:
            return ttak_accel_run_cpu;
        case TTAK_ACCEL_TARGET_CUDA:
#ifdef ENABLE_CUDA
            return ttak_accel_run_cuda;
#else
            break;
#endif
        case TTAK_ACCEL_TARGET_OPENCL:
#ifdef ENABLE_OPENCL
            return ttak_accel_run_opencl;
#else
            break;
#endif
        case TTAK_ACCEL_TARGET_ROCM:
#ifdef ENABLE_ROCM
            return ttak_accel_run_rocm;
#else
            break;
#endif
        default:
            break;
    }
    return NULL;
}

static ttak_accel_backend_fn ttak_attempt_bind(ttak_accel_target_t requested,
                                               ttak_accel_target_t *bound_target) {
    uintptr_t cached = atomic_load_explicit(&g_backend_ptr, memory_order_acquire);
    if (cached != 0) {
        if (bound_target != NULL) {
            *bound_target = atomic_load_explicit(&g_backend_target, memory_order_relaxed);
        }
        return (ttak_accel_backend_fn)(uintptr_t)cached;
    }

    ttak_accel_backend_fn candidate = ttak_backend_for_target(requested);
    ttak_accel_target_t selected = requested;

    if (candidate == NULL) {
        /* Fallback order: CUDA -> ROCm -> OpenCL -> CPU */
#ifdef ENABLE_CUDA
        candidate = ttak_accel_run_cuda;
        selected = TTAK_ACCEL_TARGET_CUDA;
#endif
#ifdef ENABLE_ROCM
        if (candidate == NULL) {
            candidate = ttak_accel_run_rocm;
            selected = TTAK_ACCEL_TARGET_ROCM;
        }
#endif
#ifdef ENABLE_OPENCL
        if (candidate == NULL) {
            candidate = ttak_accel_run_opencl;
            selected = TTAK_ACCEL_TARGET_OPENCL;
        }
#endif
        if (candidate == NULL) {
            candidate = ttak_accel_run_cpu;
            selected = TTAK_ACCEL_TARGET_CPU;
        }
    }

    if (candidate == NULL) {
        return NULL;
    }

    uintptr_t expected = 0;
    if (atomic_compare_exchange_strong_explicit(
            &g_backend_ptr, &expected, (uintptr_t)candidate,
            memory_order_acq_rel, memory_order_acquire)) {
        atomic_store_explicit(&g_backend_target, selected, memory_order_release);
        if (bound_target != NULL) {
            *bound_target = selected;
        }
        return candidate;
    }

    /* Another thread installed the backend. Return the installed pointer. */
    if (bound_target != NULL) {
        *bound_target = atomic_load_explicit(&g_backend_target, memory_order_relaxed);
    }
    return (ttak_accel_backend_fn)expected;
}

ttak_result_t ttak_execute_batch(
    const ttak_accel_batch_item_t *items,
    size_t item_count,
    const ttak_accel_config_t *config) {
    if (items == NULL || item_count == 0) {
        return TTAK_RESULT_ERR_ARGUMENT;
    }

    ttak_accel_config_t safe_cfg = {
        .preferred_target = TTAK_ACCEL_TARGET_CPU,
        .max_tiles = item_count,
        .integrity_mask = 0xFFFFFFFFu
    };

    if (config != NULL) {
        safe_cfg = *config;
        if (safe_cfg.max_tiles == 0) {
            safe_cfg.max_tiles = item_count;
        }
        if (safe_cfg.integrity_mask == 0) {
            safe_cfg.integrity_mask = 0xFFFFFFFFu;
        }
    }

    ttak_accel_target_t chosen_target = safe_cfg.preferred_target;
    ttak_accel_backend_fn backend = ttak_attempt_bind(safe_cfg.preferred_target,
                                                      &chosen_target);
    if (backend == NULL) {
        return TTAK_RESULT_ERR_UNSUPPORTED;
    }

    return backend(items, item_count, &safe_cfg);
}

ttak_result_t ttak_get_active_backend(ttak_accel_backend_fn *out_fn,
                                      ttak_accel_target_t *out_target) {
    uintptr_t cached = atomic_load_explicit(&g_backend_ptr, memory_order_acquire);
    if (cached == 0) {
        ttak_accel_target_t selected = TTAK_ACCEL_TARGET_CPU;
        ttak_accel_backend_fn backend = ttak_attempt_bind(selected, &selected);
        if (backend == NULL) {
            return TTAK_RESULT_ERR_UNSUPPORTED;
        }
        cached = (uintptr_t)backend;
    }

    if (out_fn != NULL) {
        *out_fn = (ttak_accel_backend_fn)cached;
    }
    if (out_target != NULL) {
        *out_target = atomic_load_explicit(&g_backend_target, memory_order_relaxed);
    }
    return TTAK_RESULT_OK;
}

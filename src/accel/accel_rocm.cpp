#include "ttak/ttak_accelerator.h"

#ifdef ENABLE_ROCM
#include <hip/hip_runtime.h>
#endif

extern "C" ttak_result_t ttak_accel_run_cpu(
    const ttak_accel_batch_item_t *items,
    size_t item_count,
    const ttak_accel_config_t *config);

#ifdef ENABLE_ROCM
static bool ttak_rocm_context_ready(void) {
    static bool checked = false;
    static bool ready = false;
    if (checked) {
        return ready;
    }

    int device_count = 0;
    hipError_t err = hipInit(0);
    if (err == hipSuccess) {
        err = hipGetDeviceCount(&device_count);
    }
    ready = (err == hipSuccess && device_count > 0);
    checked = true;
    return ready;
}
#endif

extern "C" ttak_result_t ttak_accel_run_rocm(
    const ttak_accel_batch_item_t *items,
    size_t item_count,
    const ttak_accel_config_t *config) {
#ifdef ENABLE_ROCM
    if (items == nullptr || config == nullptr) {
        return TTAK_RESULT_ERR_ARGUMENT;
    }

    if (!ttak_rocm_context_ready()) {
        return ttak_accel_run_cpu(items, item_count, config);
    }

    /* GPU kernels are not wired yet; keep parity by falling back after
     * verifying that the runtime is available.
     */
    return ttak_accel_run_cpu(items, item_count, config);
#else
    (void)items;
    (void)item_count;
    (void)config;
    return ttak_accel_run_cpu(items, item_count, config);
#endif
}

#include "ttak/ttak_accelerator.h"

extern "C" ttak_result_t ttak_accel_run_cpu(
    const ttak_accel_batch_item_t *items,
    size_t item_count,
    const ttak_accel_config_t *config);

/* HIP/ROCm requires a C++ compiler, hence this translation unit is C++ even
 * though the surrounding project stays in C. The function still exposes a C ABI.
 */
extern "C" ttak_result_t ttak_accel_run_rocm(
    const ttak_accel_batch_item_t *items,
    size_t item_count,
    const ttak_accel_config_t *config) {
    (void)items;
    (void)item_count;
    (void)config;
    return ttak_accel_run_cpu(items, item_count, config);
}

#ifndef TTAK_ACCELERATOR_H
#define TTAK_ACCELERATOR_H

#include <stddef.h>
#include <stdint.h>
#include <stdatomic.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Result codes returned by accelerator backends.
 */
typedef enum ttak_result {
    TTAK_RESULT_OK = 0,
    TTAK_RESULT_ERR_ARGUMENT = -1,
    TTAK_RESULT_ERR_UNSUPPORTED = -2,
    TTAK_RESULT_ERR_EXECUTION = -3
} ttak_result_t;

/**
 * @brief Physical execution targets supported by libttak.
 */
typedef enum ttak_accel_target {
    TTAK_ACCEL_TARGET_CPU = 0,
    TTAK_ACCEL_TARGET_CUDA,
    TTAK_ACCEL_TARGET_OPENCL,
    TTAK_ACCEL_TARGET_ROCM
} ttak_accel_target_t;

/**
 * @brief Batch-level configuration knobs shared by all targets.
 */
typedef struct ttak_accel_config {
    ttak_accel_target_t preferred_target;
    size_t max_tiles;
    uint32_t integrity_mask;
} ttak_accel_config_t;

/**
 * @brief Work fragment passed to an accelerator implementation.
 *
 * All pointers must remain valid for the duration of the call.
 * Every buffer is guarded with a byte length to preserve bounds.
 */
typedef struct ttak_accel_batch_item {
    const uint8_t *input;
    size_t input_len;
    uint8_t *output;
    size_t output_len;
    uint32_t mask_seed;
    uint32_t checksum_salt;
    uint32_t *checksum_out;
} ttak_accel_batch_item_t;

typedef ttak_result_t (*ttak_accel_backend_fn)(
    const ttak_accel_batch_item_t *items,
    size_t item_count,
    const ttak_accel_config_t *config);

/**
 * @brief Executes a batch on the highest priority available accelerator.
 *
 * The dispatcher uses lock-free atomics to lazily bind the backend the first time
 * this function is invoked so there is no global mutex contention after startup.
 */
ttak_result_t ttak_execute_batch(
    const ttak_accel_batch_item_t *items,
    size_t item_count,
    const ttak_accel_config_t *config);

/**
 * @brief Returns the currently selected backend implementation.
 *
 * @param out_fn Optional pointer to store the backend function pointer.
 * @param out_target Optional pointer to store which target backs the pointer.
 */
ttak_result_t ttak_get_active_backend(ttak_accel_backend_fn *out_fn,
                                      ttak_accel_target_t *out_target);

#ifdef __cplusplus
}
#endif

#endif /* TTAK_ACCELERATOR_H */

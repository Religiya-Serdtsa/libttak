/**
 * @file lea.h
 * @brief LEA (Lightweight Encryption Algorithm) block cipher interface.
 *
 * LEA is a Korean national standard block cipher (KS X 3246) designed for
 * efficient software implementations on 32-bit platforms.  Block size is
 * fixed at 16 bytes; supported key lengths are 128, 192, and 256 bits.
 */

#ifndef TTAK_SECURITY_LEA_H
#define TTAK_SECURITY_LEA_H

#include <stddef.h>
#include <stdint.h>

#include <ttak/security/security_engine.h>

#ifdef __cplusplus
extern "C" {
#endif

/** @brief AES-compatible block size used by LEA (16 bytes). */
#define TTAK_LEA_BLOCK_SIZE 16U
/** @brief Maximum number of rounds for a 256-bit LEA key schedule. */
#define TTAK_LEA_MAX_ROUNDS 32U

/**
 * @brief Pre-expanded LEA round-key schedule.
 *
 * Populated by ttak_lea_schedule_init().  The @c rounds field is set to
 * 32 (256-bit), 28 (192-bit), or 24 (128-bit) depending on key length.
 */
typedef struct ttak_lea_schedule {
    uint32_t round_keys[TTAK_LEA_MAX_ROUNDS * 6U]; /**< Expanded sub-keys. */
    size_t rounds;                                  /**< Active round count. */
} ttak_lea_schedule_t;

/**
 * @brief Expands a raw key into a LEA round-key schedule.
 *
 * @param sched   Output schedule to populate.
 * @param key     Raw key bytes (16, 24, or 32 bytes).
 * @param key_len Length of @p key in bytes.
 */
void ttak_lea_schedule_init(ttak_lea_schedule_t *sched,
                            const uint8_t *key,
                            size_t key_len);

/**
 * @brief Encrypts data with LEA, using the best available SIMD driver.
 *
 * Input/output buffers and key material are read from @p ctx.  Block count
 * must be a multiple of @c TTAK_LEA_BLOCK_SIZE.
 *
 * @param ctx    Crypto context carrying plaintext, key, and output buffer.
 * @param driver Driver descriptor from ttak_security_pick_driver().
 * @return       @c TTAK_IO_SUCCESS on success, or an error code.
 */
ttak_io_status_t ttak_lea_encrypt_simd(const ttak_crypto_ctx_t *ctx,
                                       const ttak_security_driver_t *driver);

#ifdef __cplusplus
}
#endif

#endif /* TTAK_SECURITY_LEA_H */

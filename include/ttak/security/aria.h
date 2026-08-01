/**
 * @file aria.h
 * @brief ARIA block cipher interface (KS X 1213 / RFC 5794).
 *
 * ARIA is a 128-bit block cipher standardised by the Korean government.
 * It supports 128, 192, and 256-bit keys.  This header provides the key
 * schedule and single-block encrypt/decrypt entry points.
 */

#ifndef TTAK_SECURITY_ARIA_H
#define TTAK_SECURITY_ARIA_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** @brief ARIA block size in bytes (128 bits). */
#define TTAK_ARIA_BLOCK_SIZE 16U

/** @brief Maximum number of rounds for a 256-bit ARIA key. */
#define TTAK_ARIA_MAX_ROUNDS 16U

/** @brief Maximum expanded round-key size in bytes (17 rounds × 16 bytes). */
#define TTAK_ARIA_MAX_ROUND_KEY_BYTES (17U * TTAK_ARIA_BLOCK_SIZE)

/**
 * @brief Pre-expanded ARIA round-key schedule.
 *
 * Populated by ttak_aria_schedule_init().  The @c rounds field is set to
 * 12 (128-bit), 14 (192-bit), or 16 (256-bit) depending on key length.
 * Both encryption and decryption round keys are stored so that single-block
 * decrypt can run without re-deriving the master key.
 */
typedef struct ttak_aria_schedule {
    uint8_t enc_keys[TTAK_ARIA_MAX_ROUND_KEY_BYTES]; /**< Encryption sub-keys. */
    uint8_t dec_keys[TTAK_ARIA_MAX_ROUND_KEY_BYTES]; /**< Decryption sub-keys. */
    size_t rounds;                                    /**< Active round count. */
} ttak_aria_schedule_t;

/**
 * @brief Expands a raw ARIA key into a round-key schedule.
 *
 * @param sched   Output schedule to populate.
 * @param key     Raw key bytes (16, 24, or 32 bytes).
 * @param key_len Length of @p key in bytes.
 * @return        Number of rounds on success, 0 on invalid input.
 */
size_t ttak_aria_schedule_init(ttak_aria_schedule_t *sched,
                               const uint8_t *key,
                               size_t key_len);

/**
 * @brief Encrypts a single 16-byte block with ARIA.
 *
 * @param out   Output ciphertext block (16 bytes).
 * @param in    Input plaintext block (16 bytes).
 * @param sched Pre-expanded round-key schedule.
 */
void ttak_aria_encrypt_block(uint8_t *out,
                             const uint8_t *in,
                             const ttak_aria_schedule_t *sched);

/**
 * @brief Decrypts a single 16-byte block with ARIA.
 *
 * @param out   Output plaintext block (16 bytes).
 * @param in    Input ciphertext block (16 bytes).
 * @param sched Pre-expanded round-key schedule.
 */
void ttak_aria_decrypt_block(uint8_t *out,
                             const uint8_t *in,
                             const ttak_aria_schedule_t *sched);

/**
 * @brief Securely erases a key schedule so the expanded keys do not linger
 *        in memory after use.
 *
 * @param sched Schedule to wipe.  May be NULL (no-op).
 */
void ttak_aria_schedule_wipe(ttak_aria_schedule_t *sched);

#ifdef __cplusplus
}
#endif

#endif /* TTAK_SECURITY_ARIA_H */

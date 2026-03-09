/**
 * @file security_engine.h
 * @brief Unified cryptographic dispatch engine for LibTTAK.
 *
 * Provides a single entry point (@c ttak_security_execute) that routes
 * encryption, hashing, and KDF requests to the best available driver
 * (scalar, SIMD, or hardware accelerator) selected at runtime.
 *
 * Supported operations: LEA, SEED, AES-256-GCM, ChaCha20-Poly1305,
 * post-quantum signatures, fast hashing, and memory-hard KDF.
 */

#ifndef TTAK_SECURITY_ENGINE_H
#define TTAK_SECURITY_ENGINE_H

#include <stddef.h>
#include <stdint.h>
#include <stdalign.h>

#include <ttak/io/io.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Cryptographic operation selector passed to ttak_security_execute().
 */
typedef enum ttak_security_op {
    TTAK_SECURITY_LEA_ENC = 0,         /**< LEA block-cipher encryption. */
    TTAK_SECURITY_SEED_ENC,             /**< SEED block-cipher encryption. */
    TTAK_SECURITY_AES_GCM,              /**< AES-256-GCM authenticated encryption. */
    TTAK_SECURITY_CHACHA20_POLY1305,    /**< ChaCha20-Poly1305 AEAD. */
    TTAK_SECURITY_SIGN_PQC,             /**< Post-quantum signature (stub). */
    TTAK_SECURITY_HASH_FAST,            /**< Fast non-cryptographic hash. */
    TTAK_SECURITY_KDF_HARD              /**< Memory-hard key derivation. */
} ttak_security_op_t;

/**
 * @brief Execution back-end kind for the security driver.
 */
typedef enum ttak_security_driver_kind {
    TTAK_SECURITY_DRIVER_SCALAR = 0, /**< Pure C scalar path. */
    TTAK_SECURITY_DRIVER_SIMD,       /**< SIMD-accelerated path (AVX2/NEON). */
    TTAK_SECURITY_DRIVER_ACCEL       /**< Hardware crypto engine or GPU. */
} ttak_security_driver_kind_t;

/**
 * @brief Runtime descriptor for a cryptographic driver backend.
 */
typedef struct ttak_security_driver {
    ttak_security_driver_kind_t kind; /**< Back-end kind. */
    const char *name;                 /**< Human-readable driver name. */
    size_t lane_width;                /**< Preferred parallel block count. */
} ttak_security_driver_t;

/**
 * @brief Unified I/O context passed to every cipher and hash operation.
 *
 * Callers populate the relevant fields for their operation and pass this
 * structure to ttak_security_execute().  Fields that do not apply to a
 * given operation may be left zeroed.
 */
typedef struct ttak_crypto_ctx {
    const uint8_t *in;          /**< Plaintext / input buffer. */
    size_t in_len;              /**< Length of @c in in bytes. */
    uint8_t *out;               /**< Ciphertext / output buffer. */
    size_t out_len;             /**< Capacity of @c out in bytes. */
    const uint8_t *key;         /**< Raw key material. */
    size_t key_len;             /**< Key length in bytes. */
    void *scratch;              /**< Optional caller-provided scratch space. */
    size_t scratch_len;         /**< Size of @c scratch in bytes. */
    const uint8_t *aad;         /**< Additional authenticated data (AEAD). */
    size_t aad_len;             /**< Length of @c aad in bytes. */
    uint8_t iv[16];             /**< Initialisation vector / nonce. */
    size_t iv_len;              /**< Active IV length in bytes. */
    uint8_t *tag;               /**< Authentication tag output (AEAD). */
    size_t tag_len;             /**< Desired tag length in bytes. */
    const uint8_t **in_blocks;  /**< Optional scatter-gather input array. */
    uint8_t **out_blocks;       /**< Optional scatter-gather output array. */
    size_t block_count;         /**< Number of scatter-gather blocks. */
    size_t block_size;          /**< Size of each scatter-gather block. */
    union {
        struct {
            alignas(16) uint8_t round_keys[15][16]; /**< AES-256 round keys (15 × 16 B). */
            size_t rounds;                           /**< Active round count (14 for AES-256). */
        } aes;
        struct {
            uint32_t round_keys[32]; /**< SEED expanded round keys. */
            size_t rounds;           /**< Active round count (16 for SEED-128). */
        } seed;
    } hw_state; /**< Pre-expanded cipher state for hardware-assisted paths. */
} ttak_crypto_ctx_t;

/**
 * @brief Dispatches a cryptographic operation to the selected backend.
 *
 * @param ctx Populated crypto context.
 * @param op  Operation to perform.
 * @param now Current monotonic timestamp in nanoseconds.
 * @return    @c TTAK_IO_SUCCESS, or an error code on failure.
 */
ttak_io_status_t ttak_security_execute(ttak_crypto_ctx_t *ctx,
                                       ttak_security_op_t op,
                                       uint64_t now);

/**
 * @brief Returns the best driver for the given operation at runtime.
 *
 * @param op Requested operation.
 * @return   Pointer to a statically-allocated driver descriptor.
 */
const ttak_security_driver_t *ttak_security_pick_driver(ttak_security_op_t op);

/**
 * @brief Low-level AES-256-GCM encrypt/decrypt helper.
 *
 * @param ctx Crypto context (key, iv, aad, tag fields must be set).
 * @param in  Plaintext input buffer.
 * @param out Ciphertext output buffer (same size as @p in).
 * @param len Number of bytes to process.
 * @return    @c TTAK_IO_SUCCESS on success.
 */
ttak_io_status_t ttak_aes256_gcm_execute(ttak_crypto_ctx_t *ctx,
                                         const uint8_t *in,
                                         uint8_t *out,
                                         size_t len);

/**
 * @brief Low-level ChaCha20-Poly1305 AEAD helper.
 *
 * @param ctx Crypto context (key, iv, aad, tag fields must be set).
 * @param in  Plaintext input buffer.
 * @param out Ciphertext output buffer.
 * @param len Number of bytes to process.
 * @return    @c TTAK_IO_SUCCESS on success.
 */
ttak_io_status_t ttak_chacha20_poly1305_execute(ttak_crypto_ctx_t *ctx,
                                                const uint8_t *in,
                                                uint8_t *out,
                                                size_t len);

#ifdef __cplusplus
}
#endif

#endif /* TTAK_SECURITY_ENGINE_H */

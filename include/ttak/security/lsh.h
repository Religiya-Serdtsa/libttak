/**
 * @file lsh.h
 * @brief LSH (Lightweight Secure Hash) interface.
 *
 * Provides a standard init/update/final streaming hash API for the Korean
 * LSH family: LSH-224, LSH-256, LSH-384, and LSH-512.
 */

#ifndef TTAK_SECURITY_LSH_H
#define TTAK_SECURITY_LSH_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** @brief LSH-224 digest length in bytes. */
#define TTAK_LSH224_DIGEST_SIZE 28U

/** @brief LSH-256 digest length in bytes. */
#define TTAK_LSH256_DIGEST_SIZE 32U

/** @brief LSH-384 digest length in bytes. */
#define TTAK_LSH384_DIGEST_SIZE 48U

/** @brief LSH-512 digest length in bytes. */
#define TTAK_LSH512_DIGEST_SIZE 64U

/** @brief Largest internal block size used by any LSH variant (LSH-512). */
#define TTAK_LSH_MAX_BLOCK_SIZE 256U

/** @brief Largest chaining-variable size used by any LSH variant (LSH-512). */
#define TTAK_LSH_MAX_STATE_SIZE 128U

/**
 * @brief Supported LSH hash variants.
 */
typedef enum ttak_lsh_type {
    TTAK_LSH_224 = 0, /**< LSH-256-224 (32-bit words, 26 steps). */
    TTAK_LSH_256,     /**< LSH-256-256 (32-bit words, 26 steps). */
    TTAK_LSH_384,     /**< LSH-512-384 (64-bit words, 28 steps). */
    TTAK_LSH_512      /**< LSH-512-512 (64-bit words, 28 steps). */
} ttak_lsh_type_t;

/**
 * @brief LSH incremental hash context.
 */
typedef struct ttak_lsh_ctx {
    ttak_lsh_type_t type;   /**< Active LSH variant. */
    uint8_t buffer[TTAK_LSH_MAX_BLOCK_SIZE]; /**< Partial-block input buffer. */
    size_t buflen;          /**< Bytes currently buffered in @c buffer. */
    uint64_t total_bits;    /**< Total bits processed so far. */
    uint8_t state[TTAK_LSH_MAX_STATE_SIZE]; /**< Chaining variable. */
    size_t state_size;      /**< Chaining-variable size in bytes. */
    size_t block_size;      /**< Message block size in bytes. */
    size_t digest_size;     /**< Output digest size in bytes. */
} ttak_lsh_ctx_t;

/**
 * @brief Initialises an LSH context for the requested variant.
 *
 * @param ctx  Context to initialise.
 * @param type LSH variant to use.
 * @return     0 on success, -1 on invalid input or unsupported type.
 */
int ttak_lsh_init(ttak_lsh_ctx_t *ctx, ttak_lsh_type_t type);

/**
 * @brief Feeds bytes into an in-progress LSH computation.
 *
 * @param ctx  Active hash context.
 * @param data Input bytes.
 * @param len  Number of bytes in @p data.
 */
void ttak_lsh_update(ttak_lsh_ctx_t *ctx, const uint8_t *data, size_t len);

/**
 * @brief Feeds bits into an in-progress LSH computation.
 *
 * LSH processes messages at bit granularity.  The most significant bit of
 * @p data[0] is the first input bit.  Only the @p bitlen most significant
 * bits of the supplied byte buffer are consumed.
 *
 * @param ctx    Active hash context.
 * @param data   Input bytes; only the top @p bitlen bits are used.
 * @param bitlen Number of bits to consume from @p data.
 */
void ttak_lsh_update_bits(ttak_lsh_ctx_t *ctx, const uint8_t *data, size_t bitlen);

/**
 * @brief Securely erases an LSH context so internal state and buffered data
 *        do not linger in memory after use.
 *
 * @param ctx Context to wipe.  May be NULL (no-op).
 */
void ttak_lsh_wipe(ttak_lsh_ctx_t *ctx);

/**
 * @brief Finalises the digest and writes it to @p hash.
 *
 * The output length depends on the variant selected in ttak_lsh_init():
 * 28 bytes for LSH-224, 32 bytes for LSH-256, 48 bytes for LSH-384,
 * and 64 bytes for LSH-512.
 *
 * @param ctx  Active hash context (invalidated after this call).
 * @param hash Output buffer receiving the digest.
 */
void ttak_lsh_final(ttak_lsh_ctx_t *ctx, uint8_t *hash);

#ifdef __cplusplus
}
#endif

#endif /* TTAK_SECURITY_LSH_H */

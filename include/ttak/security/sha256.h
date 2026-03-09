/**
 * @file sha256.h
 * @brief Minimal SHA-256 hash interface.
 *
 * Provides a standard init/update/final streaming hash API.  The output
 * digest is 32 bytes (256 bits) written to the caller-supplied array in
 * ttak_sha256_final().
 */

#ifndef TTAK_SECURITY_SHA256_H_
#define TTAK_SECURITY_SHA256_H_

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Output digest length in bytes. */
#define SHA256_BLOCK_SIZE 32

/**
 * @brief SHA-256 incremental hash context.
 */
typedef struct {
    uint8_t data[64];    /**< Partial-block input buffer. */
    uint32_t datalen;    /**< Bytes currently buffered in @c data. */
    uint64_t bitlen;     /**< Total bits processed so far. */
    uint32_t state[8];   /**< Running hash state (H0–H7). */
} SHA256_CTX;

/**
 * @brief Initialises a SHA-256 context with the standard IV.
 * @param ctx Context to initialise.
 */
void sha256_init(SHA256_CTX *ctx);

/**
 * @brief Feeds bytes into an in-progress SHA-256 computation.
 *
 * @param ctx  Active hash context.
 * @param data Input bytes.
 * @param len  Number of bytes in @p data.
 */
void sha256_update(SHA256_CTX *ctx, const uint8_t data[], size_t len);

/**
 * @brief Finalises the digest and writes 32 bytes to @p hash.
 *
 * @param ctx  Active hash context (invalidated after this call).
 * @param hash Output buffer receiving the 32-byte digest.
 */
void sha256_final(SHA256_CTX *ctx, uint8_t hash[]);

#ifdef __cplusplus
}
#endif

#endif  // TTAK_SECURITY_SHA256_H_
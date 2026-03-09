/**
 * @file seed.h
 * @brief SEED block cipher interface (KS X 1213, ISO/IEC 18033-3).
 *
 * SEED is a 128-bit block cipher standardised by the Korean government.
 * Key expansion is stored in @c ttak_crypto_ctx_t::hw_state::seed and the
 * single entry point below performs aligned software encryption.
 */

#ifndef TTAK_SECURITY_SEED_H
#define TTAK_SECURITY_SEED_H

#include <stddef.h>
#include <stdint.h>

#include <ttak/security/security_engine.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Encrypts data using SEED with a 16-byte-aligned software path.
 *
 * All parameters (plaintext, output buffer, pre-expanded round keys) are
 * taken from @p ctx.  Each caller must own a separate context instance.
 *
 * @param ctx    Crypto context with input, output, and @c hw_state.seed.
 * @param driver Driver descriptor used for lane-width hints.
 * @return       @c TTAK_IO_SUCCESS on success, or an error code.
 */
ttak_io_status_t ttak_seed_encrypt_aligned(const ttak_crypto_ctx_t *ctx,
                                            const ttak_security_driver_t *driver);

#ifdef __cplusplus
}
#endif

#endif /* TTAK_SECURITY_SEED_H */

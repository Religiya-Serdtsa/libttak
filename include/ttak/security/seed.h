#ifndef TTAK_SECURITY_SEED_H
#define TTAK_SECURITY_SEED_H

#include <stddef.h>
#include <stdint.h>

#include <ttak/security/security_engine.h>

#ifdef __cplusplus
extern "C" {
#endif

ttak_io_status_t ttak_seed_encrypt_aligned(const ttak_crypto_ctx_t *ctx,
                                            const ttak_security_driver_t *driver);

#ifdef __cplusplus
}
#endif

#endif /* TTAK_SECURITY_SEED_H */

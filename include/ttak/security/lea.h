#ifndef TTAK_SECURITY_LEA_H
#define TTAK_SECURITY_LEA_H

#include <stddef.h>
#include <stdint.h>

#include <ttak/security/security_engine.h>

#ifdef __cplusplus
extern "C" {
#endif

#define TTAK_LEA_BLOCK_SIZE 16U
#define TTAK_LEA_MAX_ROUNDS 32U

typedef struct ttak_lea_schedule {
    uint32_t round_keys[TTAK_LEA_MAX_ROUNDS * 6U];
    size_t rounds;
} ttak_lea_schedule_t;

void ttak_lea_schedule_init(ttak_lea_schedule_t *sched,
                            const uint8_t *key,
                            size_t key_len);

ttak_io_status_t ttak_lea_encrypt_simd(const ttak_crypto_ctx_t *ctx,
                                       const ttak_security_driver_t *driver);

#ifdef __cplusplus
}
#endif

#endif /* TTAK_SECURITY_LEA_H */

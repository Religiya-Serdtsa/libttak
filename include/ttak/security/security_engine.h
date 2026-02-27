#ifndef TTAK_SECURITY_ENGINE_H
#define TTAK_SECURITY_ENGINE_H

#include <stddef.h>
#include <stdint.h>
#include <stdalign.h>

#include <ttak/io/io.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum ttak_security_op {
    TTAK_SECURITY_LEA_ENC = 0,
    TTAK_SECURITY_SEED_ENC,
    TTAK_SECURITY_AES_GCM,
    TTAK_SECURITY_CHACHA20_POLY1305,
    TTAK_SECURITY_SIGN_PQC,
    TTAK_SECURITY_HASH_FAST,
    TTAK_SECURITY_KDF_HARD
} ttak_security_op_t;

typedef enum ttak_security_driver_kind {
    TTAK_SECURITY_DRIVER_SCALAR = 0,
    TTAK_SECURITY_DRIVER_SIMD,
    TTAK_SECURITY_DRIVER_ACCEL
} ttak_security_driver_kind_t;

typedef struct ttak_security_driver {
    ttak_security_driver_kind_t kind;
    const char *name;
    size_t lane_width;
} ttak_security_driver_t;

typedef struct ttak_crypto_ctx {
    const uint8_t *in;
    size_t in_len;
    uint8_t *out;
    size_t out_len;
    const uint8_t *key;
    size_t key_len;
    void *scratch;
    size_t scratch_len;
    const uint8_t *aad;
    size_t aad_len;
    uint8_t iv[16];
    size_t iv_len;
    uint8_t *tag;
    size_t tag_len;
    const uint8_t **in_blocks;
    uint8_t **out_blocks;
    size_t block_count;
    size_t block_size;
    union {
        struct {
            alignas(16) uint8_t round_keys[15][16];
            size_t rounds;
        } aes;
        struct {
            uint32_t round_keys[32];
            size_t rounds;
        } seed;
    } hw_state;
} ttak_crypto_ctx_t;

ttak_io_status_t ttak_security_execute(ttak_crypto_ctx_t *ctx,
                                       ttak_security_op_t op,
                                       uint64_t now);

const ttak_security_driver_t *ttak_security_pick_driver(ttak_security_op_t op);

ttak_io_status_t ttak_aes256_gcm_execute(ttak_crypto_ctx_t *ctx,
                                         const uint8_t *in,
                                         uint8_t *out,
                                         size_t len);

ttak_io_status_t ttak_chacha20_poly1305_execute(ttak_crypto_ctx_t *ctx,
                                                const uint8_t *in,
                                                uint8_t *out,
                                                size_t len);

#ifdef __cplusplus
}
#endif

#endif /* TTAK_SECURITY_ENGINE_H */

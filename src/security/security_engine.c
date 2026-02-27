#include <ttak/security/security_engine.h>
#include <ttak/security/lea.h>
#include <ttak/security/seed.h>
#include <ttak/security/security_engine.h>

#include <string.h>

#if defined(__GNUC__) || defined(__clang__)
#define TTAK_MAYBE_UNUSED __attribute__((unused))
#else
#define TTAK_MAYBE_UNUSED
#endif

static ttak_security_driver_t g_scalar_driver TTAK_MAYBE_UNUSED = {
    .kind = TTAK_SECURITY_DRIVER_SCALAR,
    .name = "scalar",
    .lane_width = 1
};

static ttak_security_driver_t g_simd_driver = {
    .kind = TTAK_SECURITY_DRIVER_SIMD,
    .name = "simd",
    .lane_width = 8
};

static const ttak_security_driver_t *ttak_security_detect_driver(void) {
#if defined(__AVX512F__)
    g_simd_driver.lane_width = 16;
    g_simd_driver.name = "avx512";
    return &g_simd_driver;
#elif defined(__AVX2__) || defined(__ARM_NEON) || defined(__ARM_NEON__)
    g_simd_driver.lane_width = 8;
#if defined(__ARM_NEON) || defined(__ARM_NEON__)
    g_simd_driver.name = "neon";
#else
    g_simd_driver.name = "avx2";
#endif
    return &g_simd_driver;
#else
    return &g_scalar_driver;
#endif
}

const ttak_security_driver_t *ttak_security_pick_driver(ttak_security_op_t op) {
    (void)op;
    return ttak_security_detect_driver();
}

static ttak_io_status_t ttak_security_handle_lea(ttak_crypto_ctx_t *ctx) {
    const ttak_security_driver_t *driver = ttak_security_pick_driver(TTAK_SECURITY_LEA_ENC);
    return ttak_lea_encrypt_simd(ctx, driver);
}

ttak_io_status_t ttak_security_execute(ttak_crypto_ctx_t *ctx,
                                       ttak_security_op_t op,
                                       uint64_t now) {
    (void)now;
    if (!ctx) {
        return TTAK_IO_ERR_INVALID_ARGUMENT;
    }

    switch (op) {
        case TTAK_SECURITY_LEA_ENC:
            return ttak_security_handle_lea(ctx);
        case TTAK_SECURITY_AES_GCM:
            return ttak_aes256_gcm_execute(ctx, ctx->in, ctx->out, ctx->in_len);
        case TTAK_SECURITY_CHACHA20_POLY1305:
            return ttak_chacha20_poly1305_execute(ctx, ctx->in, ctx->out, ctx->in_len);
        case TTAK_SECURITY_SEED_ENC:
            return ttak_seed_encrypt_aligned(ctx,
                                             ttak_security_pick_driver(TTAK_SECURITY_SEED_ENC));
        case TTAK_SECURITY_SIGN_PQC:
        case TTAK_SECURITY_HASH_FAST:
        case TTAK_SECURITY_KDF_HARD:
        default:
            return TTAK_IO_ERR_INVALID_ARGUMENT;
    }
}

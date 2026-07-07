/**
 * @file security_engine.c
 * @brief Runtime dispatch engine — routes crypto ops to the best driver.
 *
 * Detects SIMD capabilities (AVX-512, AVX2, NEON) at compile time and
 * selects the widest available lane width.  Falls back to a scalar driver
 * on platforms without SIMD extensions.
 */

#include <ttak/security/security_engine.h>
#include <ttak/security/aria.h>
#include <ttak/security/lea.h>
#include <ttak/security/lsh.h>
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

static ttak_io_status_t ttak_security_handle_aria_enc(ttak_crypto_ctx_t *ctx) {
    if (!ctx->in || !ctx->out || !ctx->key) {
        return TTAK_IO_ERR_INVALID_ARGUMENT;
    }
    if (ctx->in_len == 0 || (ctx->in_len % TTAK_ARIA_BLOCK_SIZE) != 0) {
        return TTAK_IO_ERR_RANGE;
    }
    if (ctx->out_len < ctx->in_len) {
        return TTAK_IO_ERR_RANGE;
    }

    ttak_aria_schedule_t sched;
    if (ttak_aria_schedule_init(&sched, ctx->key, ctx->key_len) == 0) {
        return TTAK_IO_ERR_INVALID_ARGUMENT;
    }

    size_t blocks = ctx->in_len / TTAK_ARIA_BLOCK_SIZE;
    for (size_t i = 0; i < blocks; ++i) {
        ttak_aria_encrypt_block(ctx->out + i * TTAK_ARIA_BLOCK_SIZE,
                                ctx->in + i * TTAK_ARIA_BLOCK_SIZE,
                                &sched);
    }
    ttak_aria_schedule_wipe(&sched);
    return TTAK_IO_SUCCESS;
}

static ttak_io_status_t ttak_security_handle_aria_dec(ttak_crypto_ctx_t *ctx) {
    if (!ctx->in || !ctx->out || !ctx->key) {
        return TTAK_IO_ERR_INVALID_ARGUMENT;
    }
    if (ctx->in_len == 0 || (ctx->in_len % TTAK_ARIA_BLOCK_SIZE) != 0) {
        return TTAK_IO_ERR_RANGE;
    }
    if (ctx->out_len < ctx->in_len) {
        return TTAK_IO_ERR_RANGE;
    }

    ttak_aria_schedule_t sched;
    if (ttak_aria_schedule_init(&sched, ctx->key, ctx->key_len) == 0) {
        return TTAK_IO_ERR_INVALID_ARGUMENT;
    }

    size_t blocks = ctx->in_len / TTAK_ARIA_BLOCK_SIZE;
    for (size_t i = 0; i < blocks; ++i) {
        ttak_aria_decrypt_block(ctx->out + i * TTAK_ARIA_BLOCK_SIZE,
                                ctx->in + i * TTAK_ARIA_BLOCK_SIZE,
                                &sched);
    }
    ttak_aria_schedule_wipe(&sched);
    return TTAK_IO_SUCCESS;
}

static ttak_io_status_t ttak_security_handle_lsh(ttak_crypto_ctx_t *ctx) {
    if (!ctx->in || !ctx->out) {
        return TTAK_IO_ERR_INVALID_ARGUMENT;
    }
    if (ctx->out_len < TTAK_LSH256_DIGEST_SIZE) {
        return TTAK_IO_ERR_RANGE;
    }

    ttak_lsh_type_t type;
    size_t required;
    if (ctx->out_len >= TTAK_LSH512_DIGEST_SIZE) {
        type = TTAK_LSH_512;
        required = TTAK_LSH512_DIGEST_SIZE;
    } else if (ctx->out_len >= TTAK_LSH384_DIGEST_SIZE) {
        type = TTAK_LSH_384;
        required = TTAK_LSH384_DIGEST_SIZE;
    } else if (ctx->out_len >= TTAK_LSH256_DIGEST_SIZE) {
        type = TTAK_LSH_256;
        required = TTAK_LSH256_DIGEST_SIZE;
    } else {
        return TTAK_IO_ERR_RANGE;
    }
    (void)required;

    ttak_lsh_ctx_t lsh_ctx;
    if (ttak_lsh_init(&lsh_ctx, type) != 0) {
        return TTAK_IO_ERR_INVALID_ARGUMENT;
    }
    ttak_lsh_update(&lsh_ctx, ctx->in, ctx->in_len);
    ttak_lsh_final(&lsh_ctx, ctx->out);
    ttak_lsh_wipe(&lsh_ctx);
    return TTAK_IO_SUCCESS;
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
        case TTAK_SECURITY_ARIA_ENC:
            return ttak_security_handle_aria_enc(ctx);
        case TTAK_SECURITY_ARIA_DEC:
            return ttak_security_handle_aria_dec(ctx);
        case TTAK_SECURITY_AES_GCM:
            return ttak_aes256_gcm_execute(ctx, ctx->in, ctx->out, ctx->in_len);
        case TTAK_SECURITY_CHACHA20_POLY1305:
            return ttak_chacha20_poly1305_execute(ctx, ctx->in, ctx->out, ctx->in_len);
        case TTAK_SECURITY_SEED_ENC:
            return ttak_seed_encrypt_aligned(ctx,
                                             ttak_security_pick_driver(TTAK_SECURITY_SEED_ENC));
        case TTAK_SECURITY_LSH:
            return ttak_security_handle_lsh(ctx);
        case TTAK_SECURITY_SIGN_PQC:
        case TTAK_SECURITY_HASH_FAST:
        case TTAK_SECURITY_KDF_HARD:
        default:
            return TTAK_IO_ERR_INVALID_ARGUMENT;
    }
}

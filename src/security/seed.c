#include <ttak/security/seed.h>

#include <string.h>

#include "seed_tables.h"

#define TTAK_SEED_BLOCK_BYTES 16U
#define TTAK_SEED_ROUNDS      16U

static const uint32_t ttak_seed_kc[16] = {
    0x9E3779B9U,0x3C6EF373U,0x78DDE6E6U,0xF1BBCDCCU,
    0xE3779B99U,0xC6EF3733U,0x8DDE6E67U,0x1BBCDCCFU,
    0x3779B99EU,0x6EF3733CU,0xDDE6E678U,0xBBCDCCF1U,
    0x779B99E3U,0xEF3733C6U,0xDE6E678DU,0xBCDCCF1BU
};

static inline uint32_t ttak_seed_g(uint32_t x) {
    uint32_t x3 = (uint32_t)(uint8_t)(x >> 24);
    uint32_t x2 = (uint32_t)(uint8_t)(x >> 16);
    uint32_t x1 = (uint32_t)(uint8_t)(x >> 8);
    uint32_t x0 = (uint32_t)(uint8_t)x;

    return ttak_seed_ss0[x0] ^ ttak_seed_ss1[x1] ^
           ttak_seed_ss2[x2] ^ ttak_seed_ss3[x3];
}

static inline uint32_t ttak_seed_load32(const uint8_t *src) {
    return ((uint32_t)src[0] << 24) | ((uint32_t)src[1] << 16) |
           ((uint32_t)src[2] << 8) | (uint32_t)src[3];
}

static inline void ttak_seed_store32(uint8_t *dst, uint32_t v) {
    dst[0] = (uint8_t)(v >> 24);
    dst[1] = (uint8_t)(v >> 16);
    dst[2] = (uint8_t)(v >> 8);
    dst[3] = (uint8_t)v;
}

static void ttak_seed_key_schedule(ttak_crypto_ctx_t *ctx) {
    if (ctx->hw_state.seed.rounds == TTAK_SEED_ROUNDS) {
        return;
    }

    uint32_t A = ttak_seed_load32(ctx->key + 0);
    uint32_t B = ttak_seed_load32(ctx->key + 4);
    uint32_t C = ttak_seed_load32(ctx->key + 8);
    uint32_t D = ttak_seed_load32(ctx->key + 12);

    for (size_t i = 0; i < TTAK_SEED_ROUNDS; ++i) {
        uint32_t T0 = (A + C - ttak_seed_kc[i]);
        uint32_t T1 = (B - D + ttak_seed_kc[i]);
        ctx->hw_state.seed.round_keys[i * 2]     = ttak_seed_g(T0);
        ctx->hw_state.seed.round_keys[i * 2 + 1] = ttak_seed_g(T1);

        if ((i & 1U) == 0U) {
            uint32_t tmpA = A;
            A = (A >> 8) | (B << 24);
            B = (B >> 8) | (tmpA << 24);
        } else {
            uint32_t tmpC = C;
            C = (C << 8) | (D >> 24);
            D = (D << 8) | (tmpC >> 24);
        }
    }
    ctx->hw_state.seed.rounds = TTAK_SEED_ROUNDS;
}

static inline void ttak_seed_round(uint32_t *L0, uint32_t *L1,
                                   uint32_t *R0, uint32_t *R1,
                                   uint32_t K0, uint32_t K1) {
    uint32_t T0 = *R0 ^ K0;
    uint32_t T1 = *R1 ^ K1;
    T1 ^= T0;
    T1 = ttak_seed_g(T1);
    T0 = (T0 + T1);
    T0 = ttak_seed_g(T0);
    T1 = (T1 + T0);
    T1 = ttak_seed_g(T1);
    T0 = (T0 + T1);
    *L0 ^= T0;
    *L1 ^= T1;
}

static void ttak_seed_encrypt_block(uint8_t *out, const uint8_t *in,
                                    const uint32_t *rk) {
    uint32_t L0 = ttak_seed_load32(in);
    uint32_t L1 = ttak_seed_load32(in + 4);
    uint32_t R0 = ttak_seed_load32(in + 8);
    uint32_t R1 = ttak_seed_load32(in + 12);

    for (size_t round = 0; round < TTAK_SEED_ROUNDS; ++round) {
        ttak_seed_round(&L0, &L1, &R0, &R1,
                        rk[round * 2], rk[round * 2 + 1]);
        if (round < TTAK_SEED_ROUNDS - 1) {
            uint32_t tmp0 = L0, tmp1 = L1;
            L0 = R0; L1 = R1;
            R0 = tmp0; R1 = tmp1;
        }
    }

    ttak_seed_store32(out, L0);
    ttak_seed_store32(out + 4, L1);
    ttak_seed_store32(out + 8, R0);
    ttak_seed_store32(out + 12, R1);
}

static inline size_t ttak_seed_ctx_block_size(const ttak_crypto_ctx_t *ctx) {
    return ctx->block_size ? ctx->block_size : TTAK_SEED_BLOCK_BYTES;
}

static inline size_t ttak_seed_ctx_blocks(const ttak_crypto_ctx_t *ctx,
                                          size_t block_size) {
    if (ctx->in_blocks && ctx->block_count) return ctx->block_count;
    return ctx->in_len / block_size;
}

static inline const uint8_t *ttak_seed_in_block(const ttak_crypto_ctx_t *ctx,
                                                size_t idx,
                                                size_t block_size) {
    return ctx->in_blocks ? ctx->in_blocks[idx]
                          : ctx->in + idx * block_size;
}

static inline uint8_t *ttak_seed_out_block(const ttak_crypto_ctx_t *ctx,
                                           size_t idx,
                                           size_t block_size) {
    return ctx->out_blocks ? ctx->out_blocks[idx]
                           : ctx->out + idx * block_size;
}

ttak_io_status_t ttak_seed_encrypt_aligned(const ttak_crypto_ctx_t *ctx,
                                           const ttak_security_driver_t *driver) {
    if (!ctx || (!ctx->in && !ctx->in_blocks) ||
        (!ctx->out && !ctx->out_blocks) || !ctx->key) {
        return TTAK_IO_ERR_INVALID_ARGUMENT;
    }
    if (ctx->key_len != 16) {
        return TTAK_IO_ERR_RANGE;
    }

    size_t block_size = ttak_seed_ctx_block_size(ctx);
    if (block_size != TTAK_SEED_BLOCK_BYTES) {
        return TTAK_IO_ERR_RANGE;
    }

    size_t blocks = ttak_seed_ctx_blocks(ctx, block_size);
    if (!blocks) {
        return TTAK_IO_ERR_RANGE;
    }

    ttak_seed_key_schedule((ttak_crypto_ctx_t *)ctx);

    size_t lanes = (driver && driver->lane_width) ? driver->lane_width : 1U;
    size_t offset = 0;
    while (offset < blocks) {
        size_t batch = (blocks - offset < lanes) ? (blocks - offset) : lanes;
        for (size_t i = 0; i < batch; ++i) {
            const uint8_t *src = ttak_seed_in_block(ctx, offset + i, block_size);
            uint8_t *dst = ttak_seed_out_block(ctx, offset + i, block_size);
            ttak_seed_encrypt_block(dst, src, ctx->hw_state.seed.round_keys);
        }
        offset += batch;
    }
    return TTAK_IO_SUCCESS;
}

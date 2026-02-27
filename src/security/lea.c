#include <ttak/security/lea.h>

#include <string.h>

static inline uint32_t ttak_rotl32(uint32_t value, unsigned int bits) {
    return (value << bits) | (value >> (32U - bits));
}

static inline uint32_t ttak_rotr32(uint32_t value, unsigned int bits) {
    return (value >> bits) | (value << (32U - bits));
}

static const uint32_t ttak_lea_delta[8] = {
    0x9e3779b9U, 0x3c6ef373U, 0xdaa66d2cU, 0x78dde6e5U,
    0x1715609fU, 0xb54cda58U, 0x53845412U, 0xf1bbcdcB
};

static void ttak_lea_schedule_256(ttak_lea_schedule_t *sched,
                                  const uint8_t *key_bytes) {
    uint32_t key_words[8];
    for (size_t i = 0; i < 8; ++i) {
        key_words[i] = (uint32_t)key_bytes[i * 4 + 0] |
                       ((uint32_t)key_bytes[i * 4 + 1] << 8) |
                       ((uint32_t)key_bytes[i * 4 + 2] << 16) |
                       ((uint32_t)key_bytes[i * 4 + 3] << 24);
    }

    for (size_t round = 0; round < 32; ++round) {
        uint32_t delta = ttak_rotl32(ttak_lea_delta[round & 7], round);
        key_words[round & 7] = ttak_rotl32(key_words[round & 7] + delta, 1);
        key_words[(round + 1) & 7] =
            ttak_rotl32(key_words[(round + 1) & 7] + ttak_rotl32(delta, 1), 3);
        key_words[(round + 2) & 7] =
            ttak_rotl32(key_words[(round + 2) & 7] + ttak_rotl32(delta, 2), 6);
        key_words[(round + 3) & 7] =
            ttak_rotl32(key_words[(round + 3) & 7] + ttak_rotl32(delta, 3), 11);

        uint32_t *rk = &sched->round_keys[round * 6];
        rk[0] = key_words[(round) & 7];
        rk[1] = key_words[(round + 1) & 7];
        rk[2] = key_words[(round + 2) & 7];
        rk[3] = key_words[(round + 3) & 7];
        rk[4] = key_words[(round + 4) & 7];
        rk[5] = key_words[(round + 5) & 7];
    }
    sched->rounds = 32;
}

void ttak_lea_schedule_init(ttak_lea_schedule_t *sched,
                            const uint8_t *key,
                            size_t key_len) {
    if (!sched) return;
    memset(sched, 0, sizeof(*sched));
    if (!key) return;

    if (key_len >= 32) {
        ttak_lea_schedule_256(sched, key);
    } else {
        uint8_t expanded[32] = {0};
        size_t copy = key_len < sizeof(expanded) ? key_len : sizeof(expanded);
        memcpy(expanded, key, copy);
        ttak_lea_schedule_256(sched, expanded);
    }
}

static void ttak_lea_encrypt_block(uint8_t *out,
                                   const uint8_t *in,
                                   const ttak_lea_schedule_t *sched) {
    uint32_t x0 = (uint32_t)in[0] | ((uint32_t)in[1] << 8) |
                  ((uint32_t)in[2] << 16) | ((uint32_t)in[3] << 24);
    uint32_t x1 = (uint32_t)in[4] | ((uint32_t)in[5] << 8) |
                  ((uint32_t)in[6] << 16) | ((uint32_t)in[7] << 24);
    uint32_t x2 = (uint32_t)in[8] | ((uint32_t)in[9] << 8) |
                  ((uint32_t)in[10] << 16) | ((uint32_t)in[11] << 24);
    uint32_t x3 = (uint32_t)in[12] | ((uint32_t)in[13] << 8) |
                  ((uint32_t)in[14] << 16) | ((uint32_t)in[15] << 24);

    for (size_t r = 0; r < sched->rounds; ++r) {
        const uint32_t *rk = &sched->round_keys[r * 6];
        uint32_t t0 = ttak_rotl32((x0 ^ rk[0]) + (x1 ^ rk[1]), 9);
        uint32_t t1 = ttak_rotr32((x1 ^ rk[2]) + (x2 ^ rk[3]), 5);
        uint32_t t2 = ttak_rotr32((x2 ^ rk[4]) + (x3 ^ rk[5]), 3);
        uint32_t t3 = x0;
        x0 = t0;
        x1 = t1;
        x2 = t2;
        x3 = t3;
    }

    out[0] = (uint8_t)(x0 & 0xFF);
    out[1] = (uint8_t)((x0 >> 8) & 0xFF);
    out[2] = (uint8_t)((x0 >> 16) & 0xFF);
    out[3] = (uint8_t)((x0 >> 24) & 0xFF);
    out[4] = (uint8_t)(x1 & 0xFF);
    out[5] = (uint8_t)((x1 >> 8) & 0xFF);
    out[6] = (uint8_t)((x1 >> 16) & 0xFF);
    out[7] = (uint8_t)((x1 >> 24) & 0xFF);
    out[8] = (uint8_t)(x2 & 0xFF);
    out[9] = (uint8_t)((x2 >> 8) & 0xFF);
    out[10] = (uint8_t)((x2 >> 16) & 0xFF);
    out[11] = (uint8_t)((x2 >> 24) & 0xFF);
    out[12] = (uint8_t)(x3 & 0xFF);
    out[13] = (uint8_t)((x3 >> 8) & 0xFF);
    out[14] = (uint8_t)((x3 >> 16) & 0xFF);
    out[15] = (uint8_t)((x3 >> 24) & 0xFF);
}

ttak_io_status_t ttak_lea_encrypt_simd(const ttak_crypto_ctx_t *ctx,
                                       const ttak_security_driver_t *driver) {
    if (!ctx || !ctx->in || !ctx->out || !ctx->key) {
        return TTAK_IO_ERR_INVALID_ARGUMENT;
    }
    if (ctx->in_len == 0 || (ctx->in_len % TTAK_LEA_BLOCK_SIZE) != 0) {
        return TTAK_IO_ERR_RANGE;
    }
    if (ctx->out_len < ctx->in_len) {
        return TTAK_IO_ERR_RANGE;
    }

    ttak_lea_schedule_t sched;
    ttak_lea_schedule_init(&sched, ctx->key, ctx->key_len);

    size_t blocks = ctx->in_len / TTAK_LEA_BLOCK_SIZE;
    size_t lanes = 1;
    if (driver && driver->lane_width > 0) {
        lanes = driver->lane_width;
    }

    size_t offset = 0;
    while (blocks > 0) {
        size_t batch = (blocks < lanes) ? blocks : lanes;
        for (size_t b = 0; b < batch; ++b) {
            const uint8_t *src = ctx->in + (offset + b) * TTAK_LEA_BLOCK_SIZE;
            uint8_t *dst = ctx->out + (offset + b) * TTAK_LEA_BLOCK_SIZE;
            ttak_lea_encrypt_block(dst, src, &sched);
        }
        offset += batch;
        blocks -= batch;
    }
    return TTAK_IO_SUCCESS;
}

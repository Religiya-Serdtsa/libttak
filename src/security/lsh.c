/**
 * @file lsh.c
 * @brief Portable LSH (Lightweight Secure Hash) implementation.
 *
 * Implements the LSH-224, LSH-256, LSH-384, and LSH-512 variants from the
 * Korean LSH specification.  The implementation follows the spec directly:
 * 32-bit-word variants (LSH-224/256) and 64-bit-word variants (LSH-384/512)
 * share the same structure but use different word sizes, step counts, and
 * constants.
 */

#include <ttak/security/lsh.h>
#include <ttak/security/security_util.h>

#include <string.h>

/* -------------------------------------------------------------------------- */
/* Type-specific constants                                                    */
/* -------------------------------------------------------------------------- */

typedef struct ttak_lsh_params {
    size_t word_bits;
    size_t state_words;
    size_t block_words;
    size_t steps;
    size_t digest_bits;
    size_t digest_bytes;
} ttak_lsh_params_t;

static const ttak_lsh_params_t ttak_lsh_params[4] = {
    /* TTAK_LSH_224 */
    {32, 16, 32, 26, 224, TTAK_LSH224_DIGEST_SIZE},
    /* TTAK_LSH_256 */
    {32, 16, 32, 26, 256, TTAK_LSH256_DIGEST_SIZE},
    /* TTAK_LSH_384 */
    {64, 16, 32, 28, 384, TTAK_LSH384_DIGEST_SIZE},
    /* TTAK_LSH_512 */
    {64, 16, 32, 28, 512, TTAK_LSH512_DIGEST_SIZE},
};

/* -------------------------------------------------------------------------- */
/* Initialisation vectors (IV) from the LSH specification.                    */
/* -------------------------------------------------------------------------- */

static const uint32_t ttak_lsh224_iv[16] = {
    0x068608d3U, 0x62d8f7a7U, 0xd76652abU, 0x4c600a43U,
    0xbdc40aa8U, 0x1eca0b68U, 0xda1a89beU, 0x3147d354U,
    0x707eb4f9U, 0xf65b3862U, 0x6b0b2abeU, 0x56b8ec0aU,
    0xcf237286U, 0xee0d1727U, 0x33636595U, 0x8bb8d05fU
};

static const uint32_t ttak_lsh256_iv[16] = {
    0x46a10f1fU, 0xfddce486U, 0xb41443a8U, 0x198e6b9dU,
    0x3304388dU, 0xb0f5a3c7U, 0xb36061c4U, 0x7adbd553U,
    0x105d5378U, 0x2f74de54U, 0x5c2f2d95U, 0xf2553fbeU,
    0x8051357aU, 0x138668c8U, 0x47aa4484U, 0xe01afb41U
};

static const uint64_t ttak_lsh384_iv[16] = {
    0x53156a66292808f6ULL, 0xb2c4f362b204c2bcULL,
    0xb84b7213bfa05c4eULL, 0x976ceb7c1b299f73ULL,
    0xdf0cc63c0570ae97ULL, 0xda4441baa486ce3fULL,
    0x6559f5d9b5f2acc2ULL, 0x22dacf19b4b52a16ULL,
    0xbbcdacefde80953aULL, 0xc9891a2879725b3eULL,
    0x7c9fe6330237e440ULL, 0xa30ba550553f7431ULL,
    0xbb08043fb34e3e30ULL, 0xa0dec48d54618eadULL,
    0x150317267464bc57ULL, 0x32d1501fde63dc93ULL
};

static const uint64_t ttak_lsh512_iv[16] = {
    0xadd50f3c7f07094eULL, 0xe3f3cee8f9418a4fULL,
    0xb527ecde5b3d0ae9ULL, 0x2ef6dec68076f501ULL,
    0x8cb994cae5aca216ULL, 0xfbb9eae4bba48cc7ULL,
    0x650a526174725feaULL, 0x1f9a61a73f8d8085ULL,
    0xb6607378173b539bULL, 0x1bc99853b0c0b9edULL,
    0xdf727fc19b182d47ULL, 0xdbef360cf893a457ULL,
    0x4981f5e570147e80ULL, 0xd00c4490ca7d3e30ULL,
    0x5d73940c0e4ae1ecULL, 0x894085e2edb2d819ULL
};

/* -------------------------------------------------------------------------- */
/* Permutations and rotation amounts                                          */
/* -------------------------------------------------------------------------- */

static const size_t ttak_lsh_tau[16] = {
    3, 2, 0, 1, 7, 4, 5, 6, 11, 10, 8, 9, 15, 12, 13, 14
};

static const size_t ttak_lsh_sigma[16] = {
    6, 4, 5, 7, 12, 15, 14, 13, 2, 0, 1, 3, 8, 11, 10, 9
};

static const size_t ttak_lsh_gamma[8] = {
    0, 8, 16, 24, 24, 16, 8, 0
};

static const size_t ttak_lsh_gamma_64[8] = {
    0, 16, 32, 48, 8, 24, 40, 56
};

/* Step constants SC0 for 32-bit and 64-bit variants. */
static const uint32_t ttak_lsh_sc0_32[8] = {
    0x917caf90U, 0x6c1b10a2U, 0x6f352943U, 0xcf778243U,
    0x2ceb7472U, 0x29e96ff2U, 0x8a9ba428U, 0x2eeb2642U
};

static const uint64_t ttak_lsh_sc0_64[8] = {
    0x97884283c938982aULL, 0xba1fca93533e2355ULL,
    0xc519a2e87aeb1c03ULL, 0x9a0fc95462af17b1ULL,
    0xfc3dda8ab019a82bULL, 0x02825d079a895407ULL,
    0x79f2d0a7ee06a6f7ULL, 0xd76d15eed9fdf5feULL
};

/* -------------------------------------------------------------------------- */
/* Word-level helpers                                                         */
/* -------------------------------------------------------------------------- */

static inline uint32_t ttak_lsh_rotl32(uint32_t x, size_t n) {
    return (x << (unsigned)(n & 31U)) | (x >> (unsigned)((32 - n) & 31U));
}

static inline uint64_t ttak_lsh_rotl64(uint64_t x, size_t n) {
    return (x << (unsigned)(n & 63U)) | (x >> (unsigned)((64 - n) & 63U));
}

static inline uint32_t ttak_lsh_load32_le(const uint8_t *src) {
    return ((uint32_t)src[0]) |
           ((uint32_t)src[1] << 8) |
           ((uint32_t)src[2] << 16) |
           ((uint32_t)src[3] << 24);
}

static inline void ttak_lsh_store32_le(uint8_t *dst, uint32_t v) {
    dst[0] = (uint8_t)v;
    dst[1] = (uint8_t)(v >> 8);
    dst[2] = (uint8_t)(v >> 16);
    dst[3] = (uint8_t)(v >> 24);
}

static inline uint64_t ttak_lsh_load64_le(const uint8_t *src) {
    return ((uint64_t)src[0]) |
           ((uint64_t)src[1] << 8) |
           ((uint64_t)src[2] << 16) |
           ((uint64_t)src[3] << 24) |
           ((uint64_t)src[4] << 32) |
           ((uint64_t)src[5] << 40) |
           ((uint64_t)src[6] << 48) |
           ((uint64_t)src[7] << 56);
}

static inline void ttak_lsh_store64_le(uint8_t *dst, uint64_t v) {
    dst[0] = (uint8_t)v;
    dst[1] = (uint8_t)(v >> 8);
    dst[2] = (uint8_t)(v >> 16);
    dst[3] = (uint8_t)(v >> 24);
    dst[4] = (uint8_t)(v >> 32);
    dst[5] = (uint8_t)(v >> 40);
    dst[6] = (uint8_t)(v >> 48);
    dst[7] = (uint8_t)(v >> 56);
}

/* -------------------------------------------------------------------------- */
/* 32-bit LSH core                                                            */
/* -------------------------------------------------------------------------- */

static void ttak_lsh32_msgexp(const uint32_t *m, size_t steps, uint32_t *expanded) {
    /* expanded has (steps + 1) * 16 words. */
    for (size_t l = 0; l < 16; ++l) {
        expanded[l] = m[l];
        expanded[16 + l] = m[16 + l];
    }
    for (size_t j = 2; j <= steps; ++j) {
        const uint32_t *prev1 = expanded + (j - 1) * 16;
        const uint32_t *prev2 = expanded + (j - 2) * 16;
        uint32_t *cur = expanded + j * 16;
        for (size_t l = 0; l < 16; ++l) {
            cur[l] = prev1[l] + prev2[ttak_lsh_tau[l]];
        }
    }
}

static void ttak_lsh32_step(uint32_t *t, const uint32_t *m, size_t step_idx,
                            const uint32_t *sc) {
    size_t alpha = (step_idx % 2 == 0) ? 29U : 5U;
    size_t beta  = (step_idx % 2 == 0) ? 1U : 17U;

    /* MsgAdd */
    for (size_t l = 0; l < 16; ++l) t[l] ^= m[l];

    /* Mix */
    for (size_t l = 0; l < 8; ++l) {
        uint32_t x = t[l];
        uint32_t y = t[l + 8];
        x += y;
        x = ttak_lsh_rotl32(x, alpha);
        x ^= sc[l];
        y += x;
        y = ttak_lsh_rotl32(y, beta);
        x += y;
        y = ttak_lsh_rotl32(y, ttak_lsh_gamma[l]);
        t[l] = x;
        t[l + 8] = y;
    }

    /* WordPerm */
    uint32_t tmp[16];
    for (size_t l = 0; l < 16; ++l) tmp[l] = t[ttak_lsh_sigma[l]];
    memcpy(t, tmp, sizeof(tmp));
}

static void ttak_lsh32_compress(uint32_t *cv, const uint32_t *m, size_t steps,
                                const uint32_t *sc) {
    uint32_t expanded[(28 + 1) * 16];
    uint32_t t[16];

    ttak_lsh32_msgexp(m, steps, expanded);
    memcpy(t, cv, sizeof(t));

    for (size_t j = 0; j < steps; ++j) {
        ttak_lsh32_step(t, expanded + j * 16, j, sc + j * 8);
    }

    /* Final MsgAdd with M_Ns */
    for (size_t l = 0; l < 16; ++l) cv[l] = t[l] ^ expanded[steps * 16 + l];

    ttak_secure_zero(expanded, sizeof(expanded));
    ttak_secure_zero(t, sizeof(t));
}

/* -------------------------------------------------------------------------- */
/* 64-bit LSH core                                                            */
/* -------------------------------------------------------------------------- */

static void ttak_lsh64_msgexp(const uint64_t *m, size_t steps, uint64_t *expanded) {
    for (size_t l = 0; l < 16; ++l) {
        expanded[l] = m[l];
        expanded[16 + l] = m[16 + l];
    }
    for (size_t j = 2; j <= steps; ++j) {
        const uint64_t *prev1 = expanded + (j - 1) * 16;
        const uint64_t *prev2 = expanded + (j - 2) * 16;
        uint64_t *cur = expanded + j * 16;
        for (size_t l = 0; l < 16; ++l) {
            cur[l] = prev1[l] + prev2[ttak_lsh_tau[l]];
        }
    }
}

static void ttak_lsh64_step(uint64_t *t, const uint64_t *m, size_t step_idx,
                            const uint64_t *sc) {
    size_t alpha = (step_idx % 2 == 0) ? 23U : 7U;
    size_t beta  = (step_idx % 2 == 0) ? 59U : 3U;

    /* MsgAdd */
    for (size_t l = 0; l < 16; ++l) t[l] ^= m[l];

    /* Mix */
    for (size_t l = 0; l < 8; ++l) {
        uint64_t x = t[l];
        uint64_t y = t[l + 8];
        x += y;
        x = ttak_lsh_rotl64(x, alpha);
        x ^= sc[l];
        y += x;
        y = ttak_lsh_rotl64(y, beta);
        x += y;
        y = ttak_lsh_rotl64(y, ttak_lsh_gamma_64[l]);
        t[l] = x;
        t[l + 8] = y;
    }

    /* WordPerm */
    uint64_t tmp[16];
    for (size_t l = 0; l < 16; ++l) tmp[l] = t[ttak_lsh_sigma[l]];
    memcpy(t, tmp, sizeof(tmp));
}

static void ttak_lsh64_compress(uint64_t *cv, const uint64_t *m, size_t steps,
                                const uint64_t *sc) {
    uint64_t expanded[(28 + 1) * 16];
    uint64_t t[16];

    ttak_lsh64_msgexp(m, steps, expanded);
    memcpy(t, cv, sizeof(t));

    for (size_t j = 0; j < steps; ++j) {
        ttak_lsh64_step(t, expanded + j * 16, j, sc + j * 8);
    }

    /* Final MsgAdd with M_Ns */
    for (size_t l = 0; l < 16; ++l) cv[l] = t[l] ^ expanded[steps * 16 + l];

    ttak_secure_zero(expanded, sizeof(expanded));
    ttak_secure_zero(t, sizeof(t));
}

/* -------------------------------------------------------------------------- */
/* Step constants derivation                                                  */
/* -------------------------------------------------------------------------- */

static void ttak_lsh32_init_sc(uint32_t *sc, size_t steps) {
    for (size_t l = 0; l < 8; ++l) sc[l] = ttak_lsh_sc0_32[l];
    for (size_t j = 1; j < steps; ++j) {
        const uint32_t *prev = sc + (j - 1) * 8;
        uint32_t *cur = sc + j * 8;
        for (size_t l = 0; l < 8; ++l) {
            cur[l] = prev[l] + ttak_lsh_rotl32(prev[l], 8);
        }
    }
}

static void ttak_lsh64_init_sc(uint64_t *sc, size_t steps) {
    for (size_t l = 0; l < 8; ++l) sc[l] = ttak_lsh_sc0_64[l];
    for (size_t j = 1; j < steps; ++j) {
        const uint64_t *prev = sc + (j - 1) * 8;
        uint64_t *cur = sc + j * 8;
        for (size_t l = 0; l < 8; ++l) {
            cur[l] = prev[l] + ttak_lsh_rotl64(prev[l], 8);
        }
    }
}

/* -------------------------------------------------------------------------- */
/* Public API                                                                 */
/* -------------------------------------------------------------------------- */

int ttak_lsh_init(ttak_lsh_ctx_t *ctx, ttak_lsh_type_t type) {
    if (!ctx || type < TTAK_LSH_224 || type > TTAK_LSH_512) {
        return -1;
    }

    const ttak_lsh_params_t *p = &ttak_lsh_params[type];
    ttak_secure_zero(ctx, sizeof(*ctx));
    ctx->type = type;
    ctx->state_size = p->state_words * (p->word_bits / 8);
    ctx->block_size = p->block_words * (p->word_bits / 8);
    ctx->digest_size = p->digest_bytes;

    if (type == TTAK_LSH_224) {
        memcpy(ctx->state, ttak_lsh224_iv, ctx->state_size);
    } else if (type == TTAK_LSH_256) {
        memcpy(ctx->state, ttak_lsh256_iv, ctx->state_size);
    } else if (type == TTAK_LSH_384) {
        memcpy(ctx->state, ttak_lsh384_iv, ctx->state_size);
    } else {
        memcpy(ctx->state, ttak_lsh512_iv, ctx->state_size);
    }
    return 0;
}

static void ttak_lsh_process_block(ttak_lsh_ctx_t *ctx, const uint8_t *block) {
    const ttak_lsh_params_t *p = &ttak_lsh_params[ctx->type];

    if (p->word_bits == 32) {
        uint32_t m[32];
        uint32_t sc[28 * 8];
        for (size_t i = 0; i < p->block_words; ++i) {
            m[i] = ttak_lsh_load32_le(block + i * 4);
        }
        ttak_lsh32_init_sc(sc, p->steps);
        ttak_lsh32_compress((uint32_t *)ctx->state, m, p->steps, sc);
        ttak_secure_zero(m, sizeof(m));
        ttak_secure_zero(sc, sizeof(sc));
    } else {
        uint64_t m[32];
        uint64_t sc[28 * 8];
        for (size_t i = 0; i < p->block_words; ++i) {
            m[i] = ttak_lsh_load64_le(block + i * 8);
        }
        ttak_lsh64_init_sc(sc, p->steps);
        ttak_lsh64_compress((uint64_t *)ctx->state, m, p->steps, sc);
        ttak_secure_zero(m, sizeof(m));
        ttak_secure_zero(sc, sizeof(sc));
    }
}

void ttak_lsh_update_bits(ttak_lsh_ctx_t *ctx, const uint8_t *data, size_t bitlen) {
    if (!ctx || !data || bitlen == 0) {
        return;
    }

    /* Mixing byte-aligned and bit-granular updates is not supported. */
    if ((ctx->total_bits & 7) != 0) {
        return;
    }

    const size_t block_size = ctx->block_size;
    size_t byte_len = bitlen >> 3;
    size_t rem_bits = bitlen & 7;

    /* Drain any existing partial block. */
    if (ctx->buflen > 0) {
        size_t fill = block_size - ctx->buflen;
        if (byte_len < fill) {
            memcpy(ctx->buffer + ctx->buflen, data, byte_len);
            ctx->buflen += byte_len;
            ctx->total_bits += (uint64_t)byte_len * 8;
            if (rem_bits) {
                ctx->buffer[ctx->buflen] = data[byte_len] & ((0xffU >> rem_bits) ^ 0xffU);
                ctx->buflen++;
                ctx->total_bits += rem_bits;
            }
            return;
        }
        memcpy(ctx->buffer + ctx->buflen, data, fill);
        ttak_lsh_process_block(ctx, ctx->buffer);
        ctx->total_bits += (uint64_t)block_size * 8;
        ctx->buflen = 0;
        data += fill;
        byte_len -= fill;
    }

    /* Process whole blocks. */
    while (byte_len >= block_size) {
        ttak_lsh_process_block(ctx, data);
        ctx->total_bits += (uint64_t)block_size * 8;
        data += block_size;
        byte_len -= block_size;
    }

    /* Buffer remaining whole bytes. */
    if (byte_len > 0) {
        memcpy(ctx->buffer, data, byte_len);
        ctx->buflen = byte_len;
        ctx->total_bits += (uint64_t)byte_len * 8;
        data += byte_len;
    }

    /* Handle a trailing partial byte. */
    if (rem_bits) {
        ctx->buffer[ctx->buflen] = data[0] & ((0xffU >> rem_bits) ^ 0xffU);
        ctx->buflen++;
        ctx->total_bits += rem_bits;
    }
}

void ttak_lsh_update(ttak_lsh_ctx_t *ctx, const uint8_t *data, size_t len) {
    ttak_lsh_update_bits(ctx, data, len * 8);
}

void ttak_lsh_final(ttak_lsh_ctx_t *ctx, uint8_t *hash) {
    if (!ctx || !hash) {
        return;
    }

    const ttak_lsh_params_t *p = &ttak_lsh_params[ctx->type];
    const size_t block_size = ctx->block_size;
    size_t remain_bits = (size_t)(ctx->total_bits & 7);

    /* Append the 0x80 padding bit at the next unused bit position. */
    if (remain_bits) {
        ctx->buffer[ctx->buflen - 1] |= (uint8_t)(0x1U << (7 - remain_bits));
    } else {
        ctx->buffer[ctx->buflen] = 0x80;
        ctx->buflen++;
    }

    ttak_secure_zero(ctx->buffer + ctx->buflen, block_size - ctx->buflen);
    ttak_lsh_process_block(ctx, ctx->buffer);

    /* Finalisation: H[l] = CV[l] ^ CV[l+8], then write little-endian bytes. */
    if (p->word_bits == 32) {
        uint32_t *cv = (uint32_t *)ctx->state;
        uint32_t h[8];
        for (size_t l = 0; l < 8; ++l) h[l] = cv[l] ^ cv[l + 8];
        for (size_t i = 0; i < p->digest_bytes; ++i) {
            size_t word = i / 4;
            size_t byte = i % 4;
            hash[i] = (uint8_t)(h[word] >> (byte * 8));
        }
        ttak_secure_zero(h, sizeof(h));
    } else {
        uint64_t *cv = (uint64_t *)ctx->state;
        uint64_t h[8];
        for (size_t l = 0; l < 8; ++l) h[l] = cv[l] ^ cv[l + 8];
        for (size_t i = 0; i < p->digest_bytes; ++i) {
            size_t word = i / 8;
            size_t byte = i % 8;
            hash[i] = (uint8_t)(h[word] >> (byte * 8));
        }
        ttak_secure_zero(h, sizeof(h));
    }

    ttak_lsh_wipe(ctx);
}

void ttak_lsh_wipe(ttak_lsh_ctx_t *ctx) {
    if (!ctx) {
        return;
    }
    ttak_secure_zero(ctx, sizeof(*ctx));
}

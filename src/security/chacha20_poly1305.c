#include <ttak/security/security_engine.h>

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#define TTAK_CHACHA_CONST0 0x61707865U
#define TTAK_CHACHA_CONST1 0x3320646EU
#define TTAK_CHACHA_CONST2 0x79622D32U
#define TTAK_CHACHA_CONST3 0x6B206574U
#define TTAK_CHACHA_BLOCK_BYTES 64U
#define TTAK_POLY1305_BLOCK_BYTES 16U
#define TTAK_POLY1305_MASK26 0x3FFFFFFULL
#define TTAK_POLY1305_HIBIT (1ULL << 24)

typedef struct {
    uint64_t r[5];
    uint64_t r5[5];
    uint64_t h[5];
    uint64_t pad[2];
    uint8_t buffer[TTAK_POLY1305_BLOCK_BYTES];
    size_t buffer_used;
} ttak_poly1305_state_t;

static inline uint32_t ttak_load32_le(const uint8_t *src) {
    return ((uint32_t)src[0]) |
           ((uint32_t)src[1] << 8) |
           ((uint32_t)src[2] << 16) |
           ((uint32_t)src[3] << 24);
}

static inline uint64_t ttak_load64_le(const uint8_t *src) {
    return ((uint64_t)src[0]) |
           ((uint64_t)src[1] << 8) |
           ((uint64_t)src[2] << 16) |
           ((uint64_t)src[3] << 24) |
           ((uint64_t)src[4] << 32) |
           ((uint64_t)src[5] << 40) |
           ((uint64_t)src[6] << 48) |
           ((uint64_t)src[7] << 56);
}

static inline void ttak_store64_le(uint8_t *dst, uint64_t value) {
    dst[0] = (uint8_t)(value);
    dst[1] = (uint8_t)(value >> 8);
    dst[2] = (uint8_t)(value >> 16);
    dst[3] = (uint8_t)(value >> 24);
    dst[4] = (uint8_t)(value >> 32);
    dst[5] = (uint8_t)(value >> 40);
    dst[6] = (uint8_t)(value >> 48);
    dst[7] = (uint8_t)(value >> 56);
}

static inline uint32_t ttak_rotl32(uint32_t value, unsigned shift) {
    return (value << shift) | (value >> (32U - shift));
}

#define TTAK_CHACHA_QUARTER_ROUND(a, b, c, d) \
    do {                                      \
        (a) += (b);                           \
        (d) ^= (a);                           \
        (d) = ttak_rotl32((d), 16);           \
        (c) += (d);                           \
        (b) ^= (c);                           \
        (b) = ttak_rotl32((b), 12);           \
        (a) += (b);                           \
        (d) ^= (a);                           \
        (d) = ttak_rotl32((d), 8);            \
        (c) += (d);                           \
        (b) ^= (c);                           \
        (b) = ttak_rotl32((b), 7);            \
    } while (0)

static void ttak_chacha20_block(uint8_t out[TTAK_CHACHA_BLOCK_BYTES],
                                const uint32_t key[8],
                                uint32_t counter,
                                const uint32_t nonce[3]) {
    uint32_t state[16] = {
        TTAK_CHACHA_CONST0, TTAK_CHACHA_CONST1,
        TTAK_CHACHA_CONST2, TTAK_CHACHA_CONST3,
        key[0], key[1], key[2], key[3],
        key[4], key[5], key[6], key[7],
        counter, nonce[0], nonce[1], nonce[2]
    };

    uint32_t working[16];
    memcpy(working, state, sizeof(state));

    for (int i = 0; i < 10; ++i) {
        TTAK_CHACHA_QUARTER_ROUND(working[0], working[4], working[8], working[12]);
        TTAK_CHACHA_QUARTER_ROUND(working[1], working[5], working[9], working[13]);
        TTAK_CHACHA_QUARTER_ROUND(working[2], working[6], working[10], working[14]);
        TTAK_CHACHA_QUARTER_ROUND(working[3], working[7], working[11], working[15]);

        TTAK_CHACHA_QUARTER_ROUND(working[0], working[5], working[10], working[15]);
        TTAK_CHACHA_QUARTER_ROUND(working[1], working[6], working[11], working[12]);
        TTAK_CHACHA_QUARTER_ROUND(working[2], working[7], working[8], working[13]);
        TTAK_CHACHA_QUARTER_ROUND(working[3], working[4], working[9], working[14]);
    }

    for (int i = 0; i < 16; ++i) {
        uint32_t word = working[i] + state[i];
        out[i * 4 + 0] = (uint8_t)(word);
        out[i * 4 + 1] = (uint8_t)(word >> 8);
        out[i * 4 + 2] = (uint8_t)(word >> 16);
        out[i * 4 + 3] = (uint8_t)(word >> 24);
    }
}

static void ttak_poly1305_blocks(ttak_poly1305_state_t *st,
                                 const uint8_t *m,
                                 size_t bytes,
                                 uint64_t hibit) {
    uint64_t h0 = st->h[0];
    uint64_t h1 = st->h[1];
    uint64_t h2 = st->h[2];
    uint64_t h3 = st->h[3];
    uint64_t h4 = st->h[4];
    const uint64_t r0 = st->r[0];
    const uint64_t r1 = st->r[1];
    const uint64_t r2 = st->r[2];
    const uint64_t r3 = st->r[3];
    const uint64_t r4 = st->r[4];
    const uint64_t s1 = st->r5[1];
    const uint64_t s2 = st->r5[2];
    const uint64_t s3 = st->r5[3];
    const uint64_t s4 = st->r5[4];

    while (bytes >= TTAK_POLY1305_BLOCK_BYTES) {
        uint64_t t0 = ttak_load32_le(m + 0);
        uint64_t t1 = ttak_load32_le(m + 4);
        uint64_t t2 = ttak_load32_le(m + 8);
        uint64_t t3 = ttak_load32_le(m + 12);

        h0 += ( t0                     ) & TTAK_POLY1305_MASK26;
        h1 += ((t0 >> 26) | (t1 << 6)) & TTAK_POLY1305_MASK26;
        h2 += ((t1 >> 20) | (t2 << 12)) & TTAK_POLY1305_MASK26;
        h3 += ((t2 >> 14) | (t3 << 18)) & TTAK_POLY1305_MASK26;
        h4 += ((t3 >> 8)                ) & TTAK_POLY1305_MASK26;
        h4 += hibit;

        uint64_t d0 = (h0 * r0) + (h1 * s4) + (h2 * s3) + (h3 * s2) + (h4 * s1);
        uint64_t d1 = (h0 * r1) + (h1 * r0) + (h2 * s4) + (h3 * s3) + (h4 * s2);
        uint64_t d2 = (h0 * r2) + (h1 * r1) + (h2 * r0) + (h3 * s4) + (h4 * s3);
        uint64_t d3 = (h0 * r3) + (h1 * r2) + (h2 * r1) + (h3 * r0) + (h4 * s4);
        uint64_t d4 = (h0 * r4) + (h1 * r3) + (h2 * r2) + (h3 * r1) + (h4 * r0);

        uint64_t c = d0 >> 26;
        h0 = d0 & TTAK_POLY1305_MASK26;
        d1 += c;
        c = d1 >> 26;
        h1 = d1 & TTAK_POLY1305_MASK26;
        d2 += c;
        c = d2 >> 26;
        h2 = d2 & TTAK_POLY1305_MASK26;
        d3 += c;
        c = d3 >> 26;
        h3 = d3 & TTAK_POLY1305_MASK26;
        d4 += c;
        c = d4 >> 26;
        h4 = d4 & TTAK_POLY1305_MASK26;
        h0 += c * 5U;
        c = h0 >> 26;
        h0 &= TTAK_POLY1305_MASK26;
        h1 += c;

        m += TTAK_POLY1305_BLOCK_BYTES;
        bytes -= TTAK_POLY1305_BLOCK_BYTES;
    }

    st->h[0] = h0;
    st->h[1] = h1;
    st->h[2] = h2;
    st->h[3] = h3;
    st->h[4] = h4;
}

static void ttak_poly1305_init(ttak_poly1305_state_t *st,
                               const uint8_t key[32]) {
    uint64_t t0 = ttak_load32_le(key + 0);
    uint64_t t1 = ttak_load32_le(key + 4);
    uint64_t t2 = ttak_load32_le(key + 8);
    uint64_t t3 = ttak_load32_le(key + 12);

    st->r[0] = t0 & 0x3FFFFFFULL;
    st->r[1] = ((t0 >> 26) | (t1 << 6)) & 0x3FFFF03ULL;
    st->r[2] = ((t1 >> 20) | (t2 << 12)) & 0x3FFC0FFULL;
    st->r[3] = ((t2 >> 14) | (t3 << 18)) & 0x3F03FFFULL;
    st->r[4] = (t3 >> 8) & 0x00FFFFFULL;

    st->r5[0] = st->r[0] * 5U;
    st->r5[1] = st->r[1] * 5U;
    st->r5[2] = st->r[2] * 5U;
    st->r5[3] = st->r[3] * 5U;
    st->r5[4] = st->r[4] * 5U;

    memset(st->h, 0, sizeof(st->h));
    st->pad[0] = ttak_load64_le(key + 16);
    st->pad[1] = ttak_load64_le(key + 24);
    st->buffer_used = 0;
}

static void ttak_poly1305_update(ttak_poly1305_state_t *st,
                                 const uint8_t *m,
                                 size_t bytes) {
    if (!bytes) {
        return;
    }

    if (st->buffer_used) {
        size_t need = TTAK_POLY1305_BLOCK_BYTES - st->buffer_used;
        if (need > bytes) {
            need = bytes;
        }
        memcpy(st->buffer + st->buffer_used, m, need);
        st->buffer_used += need;
        m += need;
        bytes -= need;
        if (st->buffer_used == TTAK_POLY1305_BLOCK_BYTES) {
            ttak_poly1305_blocks(st, st->buffer, TTAK_POLY1305_BLOCK_BYTES, TTAK_POLY1305_HIBIT);
            st->buffer_used = 0;
        }
    }

    while (bytes >= TTAK_POLY1305_BLOCK_BYTES) {
        ttak_poly1305_blocks(st, m, TTAK_POLY1305_BLOCK_BYTES, TTAK_POLY1305_HIBIT);
        m += TTAK_POLY1305_BLOCK_BYTES;
        bytes -= TTAK_POLY1305_BLOCK_BYTES;
    }

    if (bytes) {
        memcpy(st->buffer, m, bytes);
        st->buffer_used = bytes;
    }
}

static void ttak_poly1305_pad16(ttak_poly1305_state_t *st) {
    if (!st->buffer_used) {
        return;
    }
    memset(st->buffer + st->buffer_used, 0,
           TTAK_POLY1305_BLOCK_BYTES - st->buffer_used);
    ttak_poly1305_blocks(st, st->buffer, TTAK_POLY1305_BLOCK_BYTES, TTAK_POLY1305_HIBIT);
    st->buffer_used = 0;
}

static void ttak_poly1305_finish(ttak_poly1305_state_t *st,
                                 uint64_t aad_len,
                                 uint64_t text_len,
                                 uint8_t mac[16]) {
    if (st->buffer_used) {
        st->buffer[st->buffer_used] = 1;
        memset(st->buffer + st->buffer_used + 1, 0,
               TTAK_POLY1305_BLOCK_BYTES - st->buffer_used - 1);
        ttak_poly1305_blocks(st, st->buffer, TTAK_POLY1305_BLOCK_BYTES, 0);
        st->buffer_used = 0;
    }

    uint8_t len_block[16];
    ttak_store64_le(len_block, aad_len);
    ttak_store64_le(len_block + 8, text_len);
    ttak_poly1305_blocks(st, len_block, sizeof(len_block), TTAK_POLY1305_HIBIT);

    uint64_t h0 = st->h[0];
    uint64_t h1 = st->h[1];
    uint64_t h2 = st->h[2];
    uint64_t h3 = st->h[3];
    uint64_t h4 = st->h[4];

    uint64_t c = h1 >> 26;
    h1 &= TTAK_POLY1305_MASK26;
    h2 += c;
    c = h2 >> 26;
    h2 &= TTAK_POLY1305_MASK26;
    h3 += c;
    c = h3 >> 26;
    h3 &= TTAK_POLY1305_MASK26;
    h4 += c;
    c = h4 >> 26;
    h4 &= TTAK_POLY1305_MASK26;
    h0 += c * 5U;
    c = h0 >> 26;
    h0 &= TTAK_POLY1305_MASK26;
    h1 += c;

    uint64_t g0 = h0 + 5U;
    c = g0 >> 26;
    g0 &= TTAK_POLY1305_MASK26;
    uint64_t g1 = h1 + c;
    c = g1 >> 26;
    g1 &= TTAK_POLY1305_MASK26;
    uint64_t g2 = h2 + c;
    c = g2 >> 26;
    g2 &= TTAK_POLY1305_MASK26;
    uint64_t g3 = h3 + c;
    c = g3 >> 26;
    g3 &= TTAK_POLY1305_MASK26;
    uint64_t g4 = h4 + c - (1ULL << 26);

    uint64_t mask = (g4 >> 63) - 1ULL;
    g0 &= mask;
    g1 &= mask;
    g2 &= mask;
    g3 &= mask;
    g4 &= mask;
    mask = ~mask;
    h0 = (h0 & mask) | g0;
    h1 = (h1 & mask) | g1;
    h2 = (h2 & mask) | g2;
    h3 = (h3 & mask) | g3;
    h4 = (h4 & mask) | g4;

    uint64_t low = h0 + (h1 << 26);
    low += (h2 & 0x0FFFULL) << 52;
    uint64_t high = (h2 >> 12);
    high += (h3 << 14);
    high += (h4 << 40);

    uint64_t pad0 = st->pad[0];
    uint64_t pad1 = st->pad[1];
    low += pad0;
    uint64_t carry = (low < pad0) ? 1ULL : 0ULL;
    high += pad1 + carry;

    ttak_store64_le(mac, low);
    ttak_store64_le(mac + 8, high);
}

static void ttak_copy_from_ctx(const uint8_t *linear,
                               const uint8_t *const *blocks,
                               size_t block_size,
                               size_t block_count,
                               size_t offset,
                               uint8_t *dst,
                               size_t bytes) {
    if (linear) {
        memcpy(dst, linear + offset, bytes);
        return;
    }
    size_t block_idx = block_size ? (offset / block_size) : 0;
    size_t block_off = block_size ? (offset % block_size) : 0;
    (void)block_count;
    while (bytes) {
        size_t chunk = block_size - block_off;
        if (chunk > bytes) {
            chunk = bytes;
        }
        memcpy(dst, blocks[block_idx] + block_off, chunk);
        dst += chunk;
        bytes -= chunk;
        block_idx++;
        block_off = 0;
    }
}

static void ttak_copy_to_ctx(uint8_t *linear,
                             uint8_t **blocks,
                             size_t block_size,
                             size_t block_count,
                             size_t offset,
                             const uint8_t *src,
                             size_t bytes) {
    if (linear) {
        memcpy(linear + offset, src, bytes);
        return;
    }
    size_t block_idx = block_size ? (offset / block_size) : 0;
    size_t block_off = block_size ? (offset % block_size) : 0;
    (void)block_count;
    while (bytes) {
        size_t chunk = block_size - block_off;
        if (chunk > bytes) {
            chunk = bytes;
        }
        memcpy(blocks[block_idx] + block_off, src, chunk);
        src += chunk;
        bytes -= chunk;
        block_idx++;
        block_off = 0;
    }
}

ttak_io_status_t ttak_chacha20_poly1305_execute(ttak_crypto_ctx_t *ctx,
                                                const uint8_t *in,
                                                uint8_t *out,
                                                size_t len) {
    if (!ctx || !ctx->key || ctx->key_len != 32 || !ctx->tag) {
        return TTAK_IO_ERR_INVALID_ARGUMENT;
    }
    if (ctx->aad_len && !ctx->aad) {
        return TTAK_IO_ERR_INVALID_ARGUMENT;
    }
    size_t iv_len = ctx->iv_len ? ctx->iv_len : 12U;
    if (iv_len != 12U) {
        return TTAK_IO_ERR_RANGE;
    }

    const uint8_t *src_linear = in ? in : ctx->in;
    uint8_t *dst_linear = out ? out : ctx->out;
    const uint8_t *const *in_blocks = ctx->in_blocks;
    uint8_t **out_blocks = ctx->out_blocks;

    if (!src_linear && !in_blocks) {
        return TTAK_IO_ERR_INVALID_ARGUMENT;
    }
    if (!dst_linear && !out_blocks) {
        return TTAK_IO_ERR_INVALID_ARGUMENT;
    }

    bool block_in = (!src_linear);
    bool block_out = (!dst_linear);
    size_t block_size = ctx->block_size;
    size_t block_count = ctx->block_count;
    size_t block_capacity = 0;

    if (block_in || block_out) {
        if (!block_size) {
            return TTAK_IO_ERR_RANGE;
        }
        if (block_count && block_size > SIZE_MAX / block_count) {
            return TTAK_IO_ERR_RANGE;
        }
        block_capacity = block_size * block_count;
    }

    size_t total_len = len;
    if (!total_len) {
        if (block_in) {
            total_len = block_capacity;
        } else {
            total_len = ctx->in_len;
        }
    }

    if (block_in && total_len > block_capacity) {
        return TTAK_IO_ERR_RANGE;
    }
    if (block_out && total_len > block_capacity) {
        return TTAK_IO_ERR_RANGE;
    }

    uint32_t key_words[8];
    for (size_t i = 0; i < 8; ++i) {
        key_words[i] = ttak_load32_le(ctx->key + i * 4);
    }
    uint32_t nonce_words[3];
    for (size_t i = 0; i < 3; ++i) {
        nonce_words[i] = ttak_load32_le(ctx->iv + i * 4);
    }

    uint8_t otk[TTAK_CHACHA_BLOCK_BYTES];
    ttak_chacha20_block(otk, key_words, 0U, nonce_words);
    ttak_poly1305_state_t poly;
    ttak_poly1305_init(&poly, otk);

    const uint8_t *aad = ctx->aad;
    if (ctx->aad_len) {
        ttak_poly1305_update(&poly, aad, ctx->aad_len);
    }
    ttak_poly1305_pad16(&poly);

    size_t offset = 0;
    uint32_t counter = 1U;
    uint8_t keystream[TTAK_CHACHA_BLOCK_BYTES];
    uint8_t buffer[TTAK_CHACHA_BLOCK_BYTES];

    while (offset < total_len) {
        size_t chunk = total_len - offset;
        if (chunk > TTAK_CHACHA_BLOCK_BYTES) {
            chunk = TTAK_CHACHA_BLOCK_BYTES;
        }
        ttak_chacha20_block(keystream, key_words, counter, nonce_words);
        counter++;
        ttak_copy_from_ctx(src_linear, in_blocks, block_size, block_count,
                           offset, buffer, chunk);
        for (size_t i = 0; i < chunk; ++i) {
            buffer[i] ^= keystream[i];
        }
        ttak_poly1305_update(&poly, buffer, chunk);
        ttak_copy_to_ctx(dst_linear, out_blocks, block_size, block_count,
                         offset, buffer, chunk);
        offset += chunk;
    }

    ttak_poly1305_pad16(&poly);

    uint8_t tag_block[16];
    uint64_t aad_len = ctx->aad_len;
    uint64_t text_len = total_len;
    ttak_poly1305_finish(&poly, aad_len, text_len, tag_block);

    size_t tag_len = ctx->tag_len ? ctx->tag_len : 16U;
    if (tag_len > 16U) {
        tag_len = 16U;
    }
    memcpy(ctx->tag, tag_block, tag_len);
    return TTAK_IO_SUCCESS;
}

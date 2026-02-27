#include <ttak/security/security_engine.h>

#if !defined(__TINYC__)
#include <immintrin.h>
#endif
#include <stdbool.h>
#include <string.h>

#if defined(__TINYC__)
/* Fallback for TCC: return error since hardware acceleration is not available */
ttak_io_status_t ttak_aes256_gcm_execute(ttak_crypto_ctx_t *ctx,
                                         const uint8_t *in,
                                         uint8_t *out,
                                         size_t len) {
    (void)ctx; (void)in; (void)out; (void)len;
    return TTAK_IO_ERR_INVALID_ARGUMENT;
}
#else
/* Original SIMD implementation for GCC/Clang */

#define TTAK_AES_GCM_BLOCK_BYTES 16U
#define TTAK_AES_GCM_VECTOR_BLOCKS 4U
#define TTAK_AES_GCM_SUPER_BLOCKS 32U

static inline uint64_t ttak_aes_bswap64(uint64_t v) {
#if defined(__GNUC__) || defined(__clang__)
    return __builtin_bswap64(v);
#else
    return ((v & 0xFFULL) << 56) |
           ((v & 0xFF00ULL) << 40) |
           ((v & 0xFF0000ULL) << 24) |
           ((v & 0xFF000000ULL) << 8) |
           ((v & 0xFF00000000ULL) >> 8) |
           ((v & 0xFF0000000000ULL) >> 24) |
           ((v & 0xFF000000000000ULL) >> 40) |
           ((v & 0xFF00000000000000ULL) >> 56);
#endif
}

#if !defined(__AVX512F__) || !defined(__AVX512DQ__) || !defined(__AVX512BW__) || \
    !defined(__AVX512VL__) || !defined(__VAES__) || !defined(__PCLMULQDQ__)

ttak_io_status_t ttak_aes256_gcm_execute(ttak_crypto_ctx_t *ctx,
                                         const uint8_t *in,
                                         uint8_t *out,
                                         size_t len) {
    (void)ctx; (void)in; (void)out; (void)len;
    return TTAK_IO_ERR_RANGE;
}

#else

static inline __m128i ttak_loadu128(const uint8_t *src) {
    return _mm_loadu_si128((const __m128i *)src);
}

static inline void ttak_storeu128(uint8_t *dst, __m128i value) {
    _mm_storeu_si128((__m128i *)dst, value);
}

static void ttak_aes256_key_schedule(ttak_crypto_ctx_t *ctx) {
    if (ctx->hw_state.aes.rounds == 14) {
        return;
    }

    __m128i r0 = ttak_loadu128(ctx->key);
    __m128i r1 = ttak_loadu128(ctx->key + 16);
    _mm_store_si128((__m128i *)ctx->hw_state.aes.round_keys[0], r0);
    _mm_store_si128((__m128i *)ctx->hw_state.aes.round_keys[1], r1);

    const uint8_t rconst[8] = {0x01, 0x02, 0x04, 0x08, 0x10, 0x20, 0x40, 0x80};
    for (int i = 2; i < 15; i += 2) {
        uint8_t rc = rconst[(i / 2 - 1) & 7];
        __m128i tmp = _mm_aeskeygenassist_si128(r1, rc);
        tmp = _mm_shuffle_epi32(tmp, _MM_SHUFFLE(3, 3, 3, 3));
        r0 = _mm_xor_si128(r0, _mm_slli_si128(r0, 4));
        r0 = _mm_xor_si128(r0, _mm_slli_si128(r0, 4));
        r0 = _mm_xor_si128(r0, _mm_slli_si128(r0, 4));
        r0 = _mm_xor_si128(r0, tmp);
        _mm_store_si128((__m128i *)ctx->hw_state.aes.round_keys[i], r0);

        tmp = _mm_aeskeygenassist_si128(r0, 0x00);
        tmp = _mm_shuffle_epi32(tmp, _MM_SHUFFLE(2, 2, 2, 2));
        r1 = _mm_xor_si128(r1, _mm_slli_si128(r1, 4));
        r1 = _mm_xor_si128(r1, _mm_slli_si128(r1, 4));
        r1 = _mm_xor_si128(r1, _mm_slli_si128(r1, 4));
        r1 = _mm_xor_si128(r1, tmp);
        _mm_store_si128((__m128i *)ctx->hw_state.aes.round_keys[i + 1], r1);
    }
    ctx->hw_state.aes.rounds = 14;
}

static inline __m128i ttak_aesenc_block(__m128i block,
                                        const ttak_crypto_ctx_t *ctx) {
    const __m128i *rk = (const __m128i *)ctx->hw_state.aes.round_keys;
    block = _mm_xor_si128(block, rk[0]);
    for (size_t r = 1; r < ctx->hw_state.aes.rounds; ++r) {
        block = _mm_aesenc_si128(block, rk[r]);
    }
    block = _mm_aesenclast_si128(block, rk[ctx->hw_state.aes.rounds]);
    return block;
}

static inline __m512i ttak_bswap512(__m512i v) {
    const __m128i rev = _mm_set_epi8(
        15, 14, 13, 12, 11, 10, 9, 8, 7, 6, 5, 4, 3, 2, 1, 0);
    const __m512i m = _mm512_broadcast_i32x4(rev);
    return _mm512_shuffle_epi8(v, m);
}

static inline __m128i ttak_ghash_reduce128(__m128i lo, __m128i hi) {
    __m128i t1 = _mm_srli_epi64(hi, 63);
    __m128i t2 = _mm_srli_epi64(hi, 62);
    __m128i t3 = _mm_srli_epi64(hi, 57);
    __m128i t = _mm_xor_si128(_mm_xor_si128(t1, t2), t3);
    __m128i u1 = _mm_slli_epi64(t, 1);
    __m128i u2 = _mm_slli_epi64(t, 2);
    __m128i u3 = _mm_slli_epi64(t, 7);
    __m128i v1 = _mm_slli_epi64(hi, 1);
    __m128i v2 = _mm_slli_epi64(hi, 2);
    __m128i v3 = _mm_slli_epi64(hi, 7);
    __m128i r = _mm_xor_si128(lo, hi);
    r = _mm_xor_si128(r, v1);
    r = _mm_xor_si128(r, v2);
    r = _mm_xor_si128(r, v3);
    r = _mm_xor_si128(r, u1);
    r = _mm_xor_si128(r, u2);
    r = _mm_xor_si128(r, u3);
    return r;
}

static inline __m128i ttak_ghash_mul128(__m128i a, __m128i b) {
    __m128i t0 = _mm_clmulepi64_si128(a, b, 0x00);
    __m128i t1 = _mm_clmulepi64_si128(a, b, 0x11);
    __m128i t2 = _mm_clmulepi64_si128(a, b, 0x10);
    __m128i t3 = _mm_clmulepi64_si128(a, b, 0x01);
    __m128i mid = _mm_xor_si128(t2, t3);
    __m128i lo = _mm_xor_si128(t0, _mm_slli_si128(mid, 8));
    __m128i hi = _mm_xor_si128(t1, _mm_srli_si128(mid, 8));
    return ttak_ghash_reduce128(lo, hi);
}

static inline __m512i ttak_ghash_reduce512(__m512i lo, __m512i hi) {
    __m512i t1 = _mm512_srli_epi64(hi, 63);
    __m512i t2 = _mm512_srli_epi64(hi, 62);
    __m512i t3 = _mm512_srli_epi64(hi, 57);
    __m512i t = _mm512_xor_si512(_mm512_xor_si512(t1, t2), t3);
    __m512i u1 = _mm512_slli_epi64(t, 1);
    __m512i u2 = _mm512_slli_epi64(t, 2);
    __m512i u3 = _mm512_slli_epi64(t, 7);
    __m512i v1 = _mm512_slli_epi64(hi, 1);
    __m512i v2 = _mm512_slli_epi64(hi, 2);
    __m512i v3 = _mm512_slli_epi64(hi, 7);
    __m512i r = _mm512_xor_si512(lo, hi);
    r = _mm512_xor_si512(r, v1);
    r = _mm512_xor_si512(r, v2);
    r = _mm512_xor_si512(r, v3);
    r = _mm512_xor_si512(r, u1);
    r = _mm512_xor_si512(r, u2);
    r = _mm512_xor_si512(r, u3);
    return r;
}

static inline __m512i ttak_ghash_mul512(__m512i a, __m512i b) {
    __m512i t0 = _mm512_clmulepi64_epi128(a, b, 0x00);
    __m512i t1 = _mm512_clmulepi64_epi128(a, b, 0x11);
    __m512i t2 = _mm512_clmulepi64_epi128(a, b, 0x10);
    __m512i t3 = _mm512_clmulepi64_epi128(a, b, 0x01);
    __m512i mid = _mm512_xor_si512(t2, t3);
    __m512i lo = _mm512_xor_si512(t0, _mm512_slli_si512(mid, 8));
    __m512i hi = _mm512_xor_si512(t1, _mm512_srli_si512(mid, 8));
    return ttak_ghash_reduce512(lo, hi);
}

static inline __m128i ttak_mm512_reduce_xor128(__m512i v) {
    __m128i x0 = _mm512_castsi512_si128(v);
    __m128i x1 = _mm512_extracti32x4_epi32(v, 1);
    __m128i x2 = _mm512_extracti32x4_epi32(v, 2);
    __m128i x3 = _mm512_extracti32x4_epi32(v, 3);
    return _mm_xor_si128(_mm_xor_si128(x0, x1), _mm_xor_si128(x2, x3));
}

static inline __m512i ttak_ghash_prepare_powers(const __m128i *hpows) {
    __m512i v = _mm512_castsi128_si512(hpows[3]);
    v = _mm512_inserti32x4(v, hpows[2], 1);
    v = _mm512_inserti32x4(v, hpows[1], 2);
    v = _mm512_inserti32x4(v, hpows[0], 3);
    return v;
}

static inline __m128i ttak_ghash_update4(__m128i acc,
                                         __m512i blocks,
                                         __m512i hpow_vec,
                                         __m128i hpow4) {
    __m128i mul = ttak_ghash_mul128(acc, hpow4);
    __m512i products = ttak_ghash_mul512(blocks, hpow_vec);
    __m128i chunk = ttak_mm512_reduce_xor128(products);
    return _mm_xor_si128(mul, chunk);
}

static __m128i ttak_ghash_process_bytes(__m128i ghash,
                                        const uint8_t *data,
                                        size_t len,
                                        const __m128i *hpows,
                                        __m512i hpow_vec,
                                        __m128i hpow4,
                                        __m128i bswap128,
                                        __m512i bswap512) {
    if (!data || !len) {
        return ghash;
    }

    size_t blocks = len / TTAK_AES_GCM_BLOCK_BYTES;
    size_t offset = 0;
    while (blocks >= TTAK_AES_GCM_VECTOR_BLOCKS) {
        __m512i chunk = _mm512_loadu_si512(data + offset);
        chunk = _mm512_shuffle_epi8(chunk, bswap512);
        ghash = ttak_ghash_update4(ghash, chunk, hpow_vec, hpow4);
        blocks -= TTAK_AES_GCM_VECTOR_BLOCKS;
        offset += TTAK_AES_GCM_VECTOR_BLOCKS * TTAK_AES_GCM_BLOCK_BYTES;
    }

    while (blocks--) {
        __m128i block = ttak_loadu128(data + offset);
        block = _mm_shuffle_epi8(block, bswap128);
        ghash = ttak_ghash_mul128(_mm_xor_si128(ghash, block), hpows[0]);
        offset += TTAK_AES_GCM_BLOCK_BYTES;
    }

    size_t rem = len & (TTAK_AES_GCM_BLOCK_BYTES - 1U);
    if (rem) {
        uint8_t tmp[16] = {0};
        memcpy(tmp, data + offset, rem);
        __m128i block = ttak_loadu128(tmp);
        block = _mm_shuffle_epi8(block, bswap128);
        ghash = ttak_ghash_mul128(_mm_xor_si128(ghash, block), hpows[0]);
    }
    return ghash;
}

static __m128i ttak_compute_j0(const ttak_crypto_ctx_t *ctx,
                               const __m128i *hpows,
                               __m512i hpow_vec,
                               __m128i hpow4,
                               __m128i bswap128,
                               __m512i bswap512) {
    size_t iv_len = ctx->iv_len ? ctx->iv_len : 12U;
    if (iv_len == 12U) {
        uint8_t block[16] = {0};
        if (ctx->iv) {
            memcpy(block, ctx->iv, 12);
        }
        block[15] = 1;
        return ttak_loadu128(block);
    }

    __m128i ghash = _mm_setzero_si128();
    ghash = ttak_ghash_process_bytes(ghash, ctx->iv, ctx->iv_len,
                                     hpows, hpow_vec, hpow4,
                                     bswap128, bswap512);
    uint64_t iv_bits = ctx->iv_len * 8ULL;
    __m128i len_block = _mm_set_epi64x((long long)ttak_aes_bswap64(iv_bits), 0);
    return ttak_ghash_mul128(_mm_xor_si128(ghash, len_block), hpows[0]);
}

static inline void ttak_ctr512_generate(__m512i *dst,
                                        size_t regs,
                                        __m128i *counter_le,
                                        __m128i bswap128) {
    const __m128i one = _mm_set_epi32(0, 0, 0, 1);
    for (size_t lane = 0; lane < regs; ++lane) {
        __m128i c0 = *counter_le;
        __m128i c1 = _mm_add_epi32(c0, one);
        __m128i c2 = _mm_add_epi32(c1, one);
        __m128i c3 = _mm_add_epi32(c2, one);
        *counter_le = _mm_add_epi32(c3, one);

        __m512i vec = _mm512_castsi128_si512(_mm_shuffle_epi8(c0, bswap128));
        vec = _mm512_inserti32x4(vec, _mm_shuffle_epi8(c1, bswap128), 1);
        vec = _mm512_inserti32x4(vec, _mm_shuffle_epi8(c2, bswap128), 2);
        vec = _mm512_inserti32x4(vec, _mm_shuffle_epi8(c3, bswap128), 3);
        dst[lane] = vec;
    }
}

static inline void ttak_aes256_encrypt_regs(__m512i *state,
                                            size_t regs,
                                            const __m512i *rk512) {
    for (size_t i = 0; i < regs; ++i) {
        state[i] = _mm512_xor_si512(state[i], rk512[0]);
    }
    for (size_t round = 1; round < 14; ++round) {
        __m512i key = rk512[round];
        for (size_t i = 0; i < regs; ++i) {
            state[i] = _mm512_aesenc_epi128(state[i], key);
        }
    }
    __m512i last = rk512[14];
    for (size_t i = 0; i < regs; ++i) {
        state[i] = _mm512_aesenclast_epi128(state[i], last);
    }
}

static inline void ttak_gather_blocks(uint8_t *dst,
                                      const uint8_t **blocks,
                                      size_t start,
                                      size_t count) {
    for (size_t i = 0; i < count; ++i) {
        memcpy(dst + i * TTAK_AES_GCM_BLOCK_BYTES,
               blocks[start + i],
               TTAK_AES_GCM_BLOCK_BYTES);
    }
}

static inline void ttak_scatter_blocks(uint8_t **blocks,
                                       size_t start,
                                       size_t count,
                                       const uint8_t *src) {
    for (size_t i = 0; i < count; ++i) {
        memcpy(blocks[start + i],
               src + i * TTAK_AES_GCM_BLOCK_BYTES,
               TTAK_AES_GCM_BLOCK_BYTES);
    }
}

static void ttak_process_blocks(__m128i *ctr_le,
                                __m128i *ghash,
                                const uint8_t *src,
                                uint8_t *dst,
                                size_t block_count,
                                const __m512i *rk512,
                                __m512i hpow_vec,
                                const __m128i *hpows,
                                __m128i hpow4,
                                __m128i bswap128,
                                __m512i bswap512) {
    size_t regs = block_count / TTAK_AES_GCM_VECTOR_BLOCKS;
    __m512i ctr_regs[8];
    __m512i state[8];
    ttak_ctr512_generate(ctr_regs, regs, ctr_le, bswap128);
    for (size_t i = 0; i < regs; ++i) {
        state[i] = ctr_regs[i];
    }
    ttak_aes256_encrypt_regs(state, regs, rk512);
    for (size_t i = 0; i < regs; ++i) {
        size_t off = i * TTAK_AES_GCM_VECTOR_BLOCKS * TTAK_AES_GCM_BLOCK_BYTES;
        __m512i pt = _mm512_loadu_si512(src + off);
        __m512i ct = _mm512_xor_si512(pt, state[i]);
        _mm512_storeu_si512(dst + off, ct);
        __m512i ct_be = _mm512_shuffle_epi8(ct, bswap512);
        *ghash = ttak_ghash_update4(*ghash, ct_be, hpow_vec, hpow4);
    }
}

ttak_io_status_t ttak_aes256_gcm_execute(ttak_crypto_ctx_t *ctx,
                                         const uint8_t *in,
                                         uint8_t *out,
                                         size_t len) {
    if (!ctx || !ctx->key || ctx->key_len != 32 || !ctx->tag) {
        return TTAK_IO_ERR_INVALID_ARGUMENT;
    }
    if (ctx->aad_len && !ctx->aad) {
        return TTAK_IO_ERR_INVALID_ARGUMENT;
    }
    if (ctx->iv_len && !ctx->iv) {
        return TTAK_IO_ERR_INVALID_ARGUMENT;
    }

    const uint8_t *src_ptr = in ? in : ctx->in;
    uint8_t *dst_ptr = out ? out : ctx->out;
    if (!src_ptr && !ctx->in_blocks) {
        return TTAK_IO_ERR_INVALID_ARGUMENT;
    }
    if (!dst_ptr && !ctx->out_blocks) {
        return TTAK_IO_ERR_INVALID_ARGUMENT;
    }

    size_t block_size = ctx->block_size ? ctx->block_size : TTAK_AES_GCM_BLOCK_BYTES;
    if (block_size != TTAK_AES_GCM_BLOCK_BYTES) {
        return TTAK_IO_ERR_RANGE;
    }

    bool block_mode = (!src_ptr && ctx->in_blocks && ctx->block_count);
    if (!len) {
        if (block_mode) {
            len = ctx->block_count * block_size;
        } else {
            len = ctx->in_len;
        }
    }

    size_t total_blocks = len / TTAK_AES_GCM_BLOCK_BYTES;
    size_t tail_bytes = len & (TTAK_AES_GCM_BLOCK_BYTES - 1U);

    ttak_aes256_key_schedule(ctx);

    const __m128i bswap_mask = _mm_set_epi8(
        15, 14, 13, 12, 11, 10, 9, 8, 7, 6, 5, 4, 3, 2, 1, 0);
    const __m512i bswap_mask512 = _mm512_broadcast_i32x4(bswap_mask);

    __m128i hash_key = ttak_aesenc_block(_mm_setzero_si128(), ctx);
    hash_key = _mm_shuffle_epi8(hash_key, bswap_mask);
    __m128i hpows[4];
    hpows[0] = hash_key;
    for (int i = 1; i < 4; ++i) {
        hpows[i] = ttak_ghash_mul128(hpows[i - 1], hash_key);
    }
    __m128i hpow4 = hpows[3];
    __m512i hpow_vec = ttak_ghash_prepare_powers(hpows);

    __m128i ghash = _mm_setzero_si128();
    ghash = ttak_ghash_process_bytes(ghash, ctx->aad, ctx->aad_len,
                                     hpows, hpow_vec, hpow4,
                                     bswap_mask, bswap_mask512);

    __m128i j0 = ttak_compute_j0(ctx,
                                 hpows, hpow_vec, hpow4,
                                 bswap_mask, bswap_mask512);
    __m128i j0_enc = ttak_aesenc_block(j0, ctx);
    __m128i ctr_le = _mm_shuffle_epi8(j0, bswap_mask);
    const __m128i one = _mm_set_epi32(0, 0, 0, 1);
    ctr_le = _mm_add_epi32(ctr_le, one);

    const __m128i *rk128 = (const __m128i *)ctx->hw_state.aes.round_keys;
    __m512i rk512[15];
    for (int i = 0; i < 15; ++i) {
        rk512[i] = _mm512_broadcast_i32x4(rk128[i]);
    }

    size_t block_idx = 0;
    alignas(64) uint8_t gather_buf[ TTAK_AES_GCM_SUPER_BLOCKS * TTAK_AES_GCM_BLOCK_BYTES ];
    alignas(64) uint8_t scatter_buf[ TTAK_AES_GCM_SUPER_BLOCKS * TTAK_AES_GCM_BLOCK_BYTES ];

    while (total_blocks >= TTAK_AES_GCM_SUPER_BLOCKS) {
        const uint8_t *chunk_in;
        uint8_t *chunk_out;
        if (block_mode) {
            ttak_gather_blocks(gather_buf, ctx->in_blocks, block_idx, TTAK_AES_GCM_SUPER_BLOCKS);
            chunk_in = gather_buf;
        } else {
            chunk_in = src_ptr + block_idx * TTAK_AES_GCM_BLOCK_BYTES;
        }
        if (dst_ptr) {
            chunk_out = dst_ptr + block_idx * TTAK_AES_GCM_BLOCK_BYTES;
        } else {
            chunk_out = scatter_buf;
        }

        ttak_process_blocks(&ctr_le, &ghash,
                            chunk_in, chunk_out,
                            TTAK_AES_GCM_SUPER_BLOCKS,
                            rk512, hpow_vec, hpows, hpow4,
                            bswap_mask, bswap_mask512);

        if (!dst_ptr && ctx->out_blocks) {
            ttak_scatter_blocks(ctx->out_blocks, block_idx,
                                TTAK_AES_GCM_SUPER_BLOCKS, chunk_out);
        }

        block_idx += TTAK_AES_GCM_SUPER_BLOCKS;
        total_blocks -= TTAK_AES_GCM_SUPER_BLOCKS;
    }

    while (total_blocks >= TTAK_AES_GCM_VECTOR_BLOCKS) {
        const uint8_t *chunk_in;
        uint8_t *chunk_out;
        if (block_mode) {
            ttak_gather_blocks(gather_buf, ctx->in_blocks, block_idx, TTAK_AES_GCM_VECTOR_BLOCKS);
            chunk_in = gather_buf;
        } else {
            chunk_in = src_ptr + block_idx * TTAK_AES_GCM_BLOCK_BYTES;
        }
        if (dst_ptr) {
            chunk_out = dst_ptr + block_idx * TTAK_AES_GCM_BLOCK_BYTES;
        } else {
            chunk_out = scatter_buf;
        }

        ttak_process_blocks(&ctr_le, &ghash,
                            chunk_in, chunk_out,
                            TTAK_AES_GCM_VECTOR_BLOCKS,
                            rk512, hpow_vec, hpows, hpow4,
                            bswap_mask, bswap_mask512);

        if (!dst_ptr && ctx->out_blocks) {
            ttak_scatter_blocks(ctx->out_blocks, block_idx,
                                TTAK_AES_GCM_VECTOR_BLOCKS, chunk_out);
        }

        block_idx += TTAK_AES_GCM_VECTOR_BLOCKS;
        total_blocks -= TTAK_AES_GCM_VECTOR_BLOCKS;
    }

    const uint8_t **in_blocks = ctx->in_blocks;
    uint8_t **out_blocks = ctx->out_blocks;
    while (total_blocks--) {
        uint8_t block[16];
        const uint8_t *src_block;
        if (block_mode) {
            src_block = in_blocks[block_idx];
        } else {
            src_block = src_ptr + block_idx * TTAK_AES_GCM_BLOCK_BYTES;
        }
        memcpy(block, src_block, 16);

        __m128i ctr_be = _mm_shuffle_epi8(ctr_le, bswap_mask);
        __m128i ks = ttak_aesenc_block(ctr_be, ctx);
        ctr_le = _mm_add_epi32(ctr_le, one);

        __m128i pt = ttak_loadu128(block);
        __m128i ct = _mm_xor_si128(pt, ks);
        ttak_storeu128(block, ct);

        __m128i ct_be = _mm_shuffle_epi8(ct, bswap_mask);
        ghash = ttak_ghash_mul128(_mm_xor_si128(ghash, ct_be), hpows[0]);

        if (dst_ptr) {
            memcpy(dst_ptr + block_idx * 16, block, 16);
        } else if (out_blocks) {
            memcpy(out_blocks[block_idx], block, 16);
        }
        block_idx++;
    }

    if (tail_bytes) {
        uint8_t pt_block[16] = {0};
        const uint8_t *tail_src = src_ptr + block_idx * 16;
        memcpy(pt_block, tail_src, tail_bytes);
        __m128i ctr_be = _mm_shuffle_epi8(ctr_le, bswap_mask);
        __m128i ks = ttak_aesenc_block(ctr_be, ctx);
        ctr_le = _mm_add_epi32(ctr_le, one);
        __m128i keystream = ks;
        uint8_t tmp[16];
        ttak_storeu128(tmp, keystream);
        for (size_t i = 0; i < tail_bytes; ++i) {
            pt_block[i] ^= tmp[i];
        }
        if (dst_ptr) {
            memcpy(dst_ptr + block_idx * 16, pt_block, tail_bytes);
        } else if (out_blocks) {
            memcpy(out_blocks[block_idx], pt_block, tail_bytes);
        }
        uint8_t pad_ct[16] = {0};
        memcpy(pad_ct, pt_block, tail_bytes);
        __m128i ct = ttak_loadu128(pad_ct);
        ct = _mm_shuffle_epi8(ct, bswap_mask);
        ghash = ttak_ghash_mul128(_mm_xor_si128(ghash, ct), hpows[0]);
    }

    uint64_t aad_bits = ctx->aad_len * 8ULL;
    uint64_t ct_bits = len * 8ULL;
    __m128i len_block = _mm_set_epi64x((long long)ttak_aes_bswap64(aad_bits),
                                       (long long)ttak_aes_bswap64(ct_bits));
    ghash = ttak_ghash_mul128(_mm_xor_si128(ghash, len_block), hpows[0]);

    __m128i tag = _mm_xor_si128(ghash, j0_enc);
    size_t tag_len = ctx->tag_len ? ctx->tag_len : 16U;
    if (tag_len > 16U) tag_len = 16U;
    uint8_t tag_buf[16];
    ttak_storeu128(tag_buf, tag);
    memcpy(ctx->tag, tag_buf, tag_len);
    return TTAK_IO_SUCCESS;
}

#endif
#endif

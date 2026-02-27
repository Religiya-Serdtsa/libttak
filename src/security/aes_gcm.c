/**
 * @file aes_gcm.c
 * @brief AES-256 GCM implementation with portable fallback and optional SIMD paths.
 *
 * This file is designed to compile cleanly across many OS/ABI targets, including
 * cross-compilers and toolchains where crypto intrinsics are not exposed unless
 * explicit target flags are provided.
 *
 * The policy is:
 *  - Prefer a compile-time SIMD path only when the compiler explicitly exposes it.
 *  - Always provide a correct, portable software fallback.
 *
 * Notes:
 *  - This code assumes ctx->hw_state.aes.round_keys stores 15 round keys for AES-256:
 *    rk[0..14], each 16 bytes (total 240 bytes).
 *  - This code performs GHASH in software. (You can later add CLMUL/PMULL/VSX paths.)
 */

#include <ttak/security/security_engine.h>

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <string.h>

#if defined(_WIN32)
#  include <windows.h>
#endif

/* --- SIMD detection --- */

/**
 * @def TTAK_AES_BLOCK_SIZE
 * @brief AES block size in bytes.
 */
#define TTAK_AES_BLOCK_SIZE 16u

/*
 * x86_64 AES-NI: require compiler to define __AES__ for AES intrinsics.
 * We intentionally do not enable it just because we are on x86_64.
 */
#if (defined(__x86_64__) || defined(_M_X64)) && defined(__AES__)
#  include <immintrin.h>
#  define TTAK_USE_X86_AESNI 1
#else
#  define TTAK_USE_X86_AESNI 0
#endif

/*
 * ARM64 Crypto: require both AArch64 and __ARM_FEATURE_CRYPTO.
 * Many cross toolchains (including Windows targets) will not define this unless
 * compiled with -march=armv8-a+crypto or equivalent.
 */
#if (defined(__aarch64__) || defined(_M_ARM64)) && defined(__ARM_FEATURE_CRYPTO)
#  include <arm_neon.h>
#  define TTAK_USE_ARM64_CRYPTO 1
#else
#  define TTAK_USE_ARM64_CRYPTO 0
#endif

/* You can extend with PPC VSX AES, MIPS, RISC-V Zk* later. */
#define TTAK_USE_PPC64LE_VSX 0
#define TTAK_USE_MIPS64_MSA  0
#define TTAK_USE_RISCV64_VV  0

/* --- Endian helpers --- */

/**
 * @brief Portable 64-bit byte swap.
 * @param v Input value.
 * @return Byte swapped value.
 */
static inline uint64_t ttak_bswap64(uint64_t v) {
#if defined(__GNUC__) || defined(__clang__)
    return __builtin_bswap64(v);
#elif defined(_MSC_VER)
    return _byteswap_uint64(v);
#else
    v = ((v << 8)  & 0xFF00FF00FF00FF00ULL) | ((v >> 8)  & 0x00FF00FF00FF00FFULL);
    v = ((v << 16) & 0xFFFF0000FFFF0000ULL) | ((v >> 16) & 0x0000FFFF0000FFFFULL);
    return (v << 32) | (v >> 32);
#endif
}

/**
 * @brief Portable 32-bit byte swap.
 * @param v Input value.
 * @return Byte swapped value.
 */
static inline uint32_t ttak_bswap32(uint32_t v) {
#if defined(__GNUC__) || defined(__clang__)
    return __builtin_bswap32(v);
#elif defined(_MSC_VER)
    return _byteswap_ulong(v);
#else
    v = ((v << 8) & 0xFF00FF00u) | ((v >> 8) & 0x00FF00FFu);
    return (v << 16) | (v >> 16);
#endif
}

/* --- GHASH (soft) --- */

/**
 * @brief XOR 16 bytes: dst ^= src
 * @param dst 16-byte destination buffer.
 * @param src 16-byte source buffer.
 */
static inline void ttak_xor16(uint8_t dst[16], const uint8_t src[16]) {
    for (int i = 0; i < 16; i++) dst[i] ^= src[i];
}

/**
 * @brief Load 64-bit big-endian from buffer (unaligned-safe).
 * @param p Pointer to at least 8 bytes.
 * @return 64-bit value interpreted as big-endian.
 */
static inline uint64_t ttak_load_be64(const uint8_t *p) {
    uint64_t v;
    memcpy(&v, p, sizeof(v));
#if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
    v = ttak_bswap64(v);
#endif
    return v;
}

/**
 * @brief Store 64-bit big-endian to buffer (unaligned-safe).
 * @param p Pointer to at least 8 bytes.
 * @param v Value to store (host order).
 */
static inline void ttak_store_be64(uint8_t *p, uint64_t v) {
#if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
    v = ttak_bswap64(v);
#endif
    memcpy(p, &v, sizeof(v));
}

/**
 * @brief GHASH multiply (Y := (Y xor X) * H) in GF(2^128), software fallback.
 *
 * This is a bitwise reference implementation. It is correct but not fast.
 * You can add CLMUL/PMULL/VSX accelerated variants later.
 *
 * @param y_acc 16-byte accumulator Y (updated in place).
 * @param h_key 16-byte hash subkey H.
 * @param x_in  16-byte input block X.
 */
static void ttak_ghash_update_soft(uint8_t y_acc[16], const uint8_t h_key[16], const uint8_t x_in[16]) {
    /* Y <- Y xor X */
    ttak_xor16(y_acc, x_in);

    /* Interpret as two 64-bit big-endian words for the classic bitwise algorithm. */
    uint64_t v0 = ttak_load_be64(h_key + 0);
    uint64_t v1 = ttak_load_be64(h_key + 8);

    uint64_t z0 = 0, z1 = 0;

    /* Process 128 bits of Y (big-endian bit order). */
    for (int i = 0; i < 128; i++) {
        const uint8_t byte = y_acc[i >> 3];
        const uint8_t mask = (uint8_t)(1u << (7 - (i & 7)));
        if (byte & mask) {
            z0 ^= v0;
            z1 ^= v1;
        }

        /* Shift V right by 1 and reduce with R = 0xE1... when LSB was 1. */
        const uint64_t lsb = (v1 & 1u);
        v1 = (v1 >> 1) | (v0 << 63);
        v0 = (v0 >> 1);
        if (lsb) {
            v0 ^= 0xE100000000000000ULL;
        }
    }

    ttak_store_be64(y_acc + 0, z0);
    ttak_store_be64(y_acc + 8, z1);
}

/* --- Portable AES-256 fallback --- */

/**
 * @brief AES S-box table.
 */
static const uint8_t ttak_aes_sbox[256] = {
    0x63,0x7c,0x77,0x7b,0xf2,0x6b,0x6f,0xc5,0x30,0x01,0x67,0x2b,0xfe,0xd7,0xab,0x76,
    0xca,0x82,0xc9,0x7d,0xfa,0x59,0x47,0xf0,0xad,0xd4,0xa2,0xaf,0x9c,0xa4,0x72,0xc0,
    0xb7,0xfd,0x93,0x26,0x36,0x3f,0xf7,0xcc,0x34,0xa5,0xe5,0xf1,0x71,0xd8,0x31,0x15,
    0x04,0xc7,0x23,0xc3,0x18,0x96,0x05,0x9a,0x07,0x12,0x80,0xe2,0xeb,0x27,0xb2,0x75,
    0x09,0x83,0x2c,0x1a,0x1b,0x6e,0x5a,0xa0,0x52,0x3b,0xd6,0xb3,0x29,0xe3,0x2f,0x84,
    0x53,0xd1,0x00,0xed,0x20,0xfc,0xb1,0x5b,0x6a,0xcb,0xbe,0x39,0x4a,0x4c,0x58,0xcf,
    0xd0,0xef,0xaa,0xfb,0x43,0x4d,0x33,0x85,0x45,0xf9,0x02,0x7f,0x50,0x3c,0x9f,0xa8,
    0x51,0xa3,0x40,0x8f,0x92,0x9d,0x38,0xf5,0xbc,0xb6,0xda,0x21,0x10,0xff,0xf3,0xd2,
    0xcd,0x0c,0x13,0xec,0x5f,0x97,0x44,0x17,0xc4,0xa7,0x7e,0x3d,0x64,0x5d,0x19,0x73,
    0x60,0x81,0x4f,0xdc,0x22,0x2a,0x90,0x88,0x46,0xee,0xb8,0x14,0xde,0x5e,0x0b,0xdb,
    0xe0,0x32,0x3a,0x0a,0x49,0x06,0x24,0x5c,0xc2,0xd3,0xac,0x62,0x91,0x95,0xe4,0x79,
    0xe7,0xc8,0x37,0x6d,0x8d,0xd5,0x4e,0xa9,0x6c,0x56,0xf4,0xea,0x65,0x7a,0xae,0x08,
    0xba,0x78,0x25,0x2e,0x1c,0xa6,0xb4,0xc6,0xe8,0xdd,0x74,0x1f,0x4b,0xbd,0x8b,0x8a,
    0x70,0x3e,0xb5,0x66,0x48,0x03,0xf6,0x0e,0x61,0x35,0x57,0xb9,0x86,0xc1,0x1d,0x9e,
    0xe1,0xf8,0x98,0x11,0x69,0xd9,0x8e,0x94,0x9b,0x1e,0x87,0xe9,0xce,0x55,0x28,0xdf,
    0x8c,0xa1,0x89,0x0d,0xbf,0xe6,0x42,0x68,0x41,0x99,0x2d,0x0f,0xb0,0x54,0xbb,0x16
};

/**
 * @brief GF(2^8) multiply by x (i.e., 2) for AES MixColumns.
 * @param x Byte.
 * @return xtime(x).
 */
static inline uint8_t ttak_aes_xtime(uint8_t x) {
    return (uint8_t)((x << 1) ^ ((x & 0x80u) ? 0x1Bu : 0x00u));
}

/**
 * @brief AES SubBytes step.
 * @param s 16-byte state.
 */
static inline void ttak_aes_subbytes(uint8_t s[16]) {
    for (int i = 0; i < 16; i++) s[i] = ttak_aes_sbox[s[i]];
}

/**
 * @brief AES ShiftRows step (in-place).
 * @param s 16-byte state.
 */
static inline void ttak_aes_shiftrows(uint8_t s[16]) {
    uint8_t t[16];

    /* Row 0: no shift */
    t[0]  = s[0];  t[4]  = s[4];  t[8]  = s[8];  t[12] = s[12];
    /* Row 1: shift left by 1 */
    t[1]  = s[5];  t[5]  = s[9];  t[9]  = s[13]; t[13] = s[1];
    /* Row 2: shift left by 2 */
    t[2]  = s[10]; t[6]  = s[14]; t[10] = s[2];  t[14] = s[6];
    /* Row 3: shift left by 3 */
    t[3]  = s[15]; t[7]  = s[3];  t[11] = s[7];  t[15] = s[11];

    memcpy(s, t, 16);
}

/**
 * @brief AES MixColumns step (in-place).
 * @param s 16-byte state.
 */
static inline void ttak_aes_mixcolumns(uint8_t s[16]) {
    for (int c = 0; c < 4; c++) {
        uint8_t *p = &s[c * 4];
        const uint8_t a0 = p[0], a1 = p[1], a2 = p[2], a3 = p[3];

        const uint8_t x0 = ttak_aes_xtime(a0);
        const uint8_t x1 = ttak_aes_xtime(a1);
        const uint8_t x2 = ttak_aes_xtime(a2);
        const uint8_t x3 = ttak_aes_xtime(a3);

        /* Multiply by 2 and 3 in GF(2^8): 3*x = 2*x xor x */
        p[0] = (uint8_t)(x0 ^ (x1 ^ a1) ^ a2 ^ a3);
        p[1] = (uint8_t)(a0 ^ x1 ^ (x2 ^ a2) ^ a3);
        p[2] = (uint8_t)(a0 ^ a1 ^ x2 ^ (x3 ^ a3));
        p[3] = (uint8_t)((x0 ^ a0) ^ a1 ^ a2 ^ x3);
    }
}

/**
 * @brief AES AddRoundKey step (in-place).
 * @param s  16-byte state.
 * @param rk 16-byte round key.
 */
static inline void ttak_aes_addroundkey(uint8_t s[16], const uint8_t rk[16]) {
    for (int i = 0; i < 16; i++) s[i] ^= rk[i];
}

/**
 * @brief Encrypt one AES-256 block using portable software rounds.
 *
 * This function expects pre-expanded round keys in ctx->hw_state.aes.round_keys.
 *
 * @param out 16-byte output.
 * @param in  16-byte input.
 * @param ctx Crypto context containing round keys.
 */
static void ttak_aes256_enc_block_soft(uint8_t out[16], const uint8_t in[16], const ttak_crypto_ctx_t *ctx) {
    const uint8_t *rk = (const uint8_t *)ctx->hw_state.aes.round_keys;

    uint8_t s[16];
    memcpy(s, in, 16);

    /* AES-256 has 14 rounds, with 15 round keys: rk[0..14]. */
    ttak_aes_addroundkey(s, rk + 0 * 16);

    for (int r = 1; r < 14; r++) {
        ttak_aes_subbytes(s);
        ttak_aes_shiftrows(s);
        ttak_aes_mixcolumns(s);
        ttak_aes_addroundkey(s, rk + r * 16);
    }

    /* Final round (no MixColumns). */
    ttak_aes_subbytes(s);
    ttak_aes_shiftrows(s);
    ttak_aes_addroundkey(s, rk + 14 * 16);

    memcpy(out, s, 16);
}

/* --- AES-256 block encryption dispatch --- */

/**
 * @brief Encrypt a single AES-256 block using the best available backend.
 *
 * This is a compile-time selection. If intrinsics are not available/exposed by the toolchain,
 * the function always falls back to portable software.
 *
 * @param out 16-byte output.
 * @param in  16-byte input.
 * @param ctx Crypto context containing round keys.
 */
static inline void ttak_aes256_enc_block(uint8_t out[16], const uint8_t in[16], const ttak_crypto_ctx_t *ctx) {
#if TTAK_USE_X86_AESNI
    const __m128i *rk = (const __m128i *)ctx->hw_state.aes.round_keys;
    __m128i b = _mm_loadu_si128((const __m128i *)in);
    b = _mm_xor_si128(b, rk[0]);
    for (int r = 1; r < 14; r++) {
        b = _mm_aesenc_si128(b, rk[r]);
    }
    b = _mm_aesenclast_si128(b, rk[14]);
    _mm_storeu_si128((__m128i *)out, b);

#elif TTAK_USE_ARM64_CRYPTO
    /*
     * ARMv8 Crypto intrinsics are only available when __ARM_FEATURE_CRYPTO is defined.
     * This avoids build failures on toolchains that target AArch64 but do not expose AES intrinsics.
     */
    const uint8x16_t *rk = (const uint8x16_t *)ctx->hw_state.aes.round_keys;
    uint8x16_t b = vld1q_u8(in);

    /* Initial AddRoundKey. */
    b = veorq_u8(b, rk[0]);

    /* 13 full rounds for AES-256: rounds 1..13 include MixColumns. */
    for (int r = 1; r < 14; r++) {
        b = vaeseq_u8(b, rk[r]);   /* SubBytes + ShiftRows + AddRoundKey */
        b = vaesmcq_u8(b);         /* MixColumns */
    }

    /* Final round: AES single round without MixColumns.
       Implement as AESE with last key then XOR with final key is not correct.
       Correct is: AESE with rk[14] and no AESMC.
       In ARM intrinsics, vaeseq_u8 performs the round including AddRoundKey.
       So we do final round directly with rk[14] but without vaesmcq_u8. */
    b = vaeseq_u8(b, rk[14]);
    vst1q_u8(out, b);

#else
    (void)ctx;
    ttak_aes256_enc_block_soft(out, in, ctx);
#endif
}

/* --- AES-256 GCM API --- */

/**
 * @brief Execute AES-256 GCM (encrypt + authenticate) with software GHASH.
 *
 * This function expects:
 *  - ctx->iv points to a 12-byte IV (96-bit nonce).
 *  - ctx->tag points to a 16-byte tag output buffer.
 *  - ctx->hw_state.aes.round_keys contains AES-256 round keys (15 x 16 bytes).
 *
 * This implementation:
 *  - Computes H = AES_K(0^128)
 *  - GHASHes AAD, then ciphertext blocks
 *  - Uses AES-CTR with counter appended to IV as 32-bit big-endian
 *  - Produces tag = GHASH(...) xor AES_K(J0)
 *
 * @param ctx Crypto context.
 * @param in  Optional input buffer (if NULL, uses ctx->in).
 * @param out Optional output buffer (if NULL, uses ctx->out).
 * @param len Optional length (if 0, uses ctx->in_len).
 * @return ttak_io_status_t
 */
ttak_io_status_t ttak_aes256_gcm_execute(ttak_crypto_ctx_t *ctx,
                                        const uint8_t *in,
                                        uint8_t *out,
                                        size_t len) {
    if (!ctx || !ctx->tag) {
        return TTAK_IO_ERR_INVALID_ARGUMENT;
    }

    const uint8_t *p_src = in ? in : ctx->in;
    uint8_t *p_dst = out ? out : ctx->out;
    const size_t d_len = (len != 0) ? len : ctx->in_len;

    if (!p_src || !p_dst) {
        return TTAK_IO_ERR_INVALID_ARGUMENT;
    }

    uint8_t h_key[16];
    uint8_t y_acc[16] = {0};
    uint8_t j0[16] = {0};
    uint8_t j0_enc[16];
    uint8_t zero_blk[16] = {0};

    /* 1) H = AES_K(0^128) */
    ttak_aes256_enc_block(h_key, zero_blk, ctx);

    /* 2) GHASH AAD */
    if (ctx->aad && ctx->aad_len > 0) {
        const size_t aad_full = ctx->aad_len / 16;
        const size_t aad_rem  = ctx->aad_len % 16;

        for (size_t i = 0; i < aad_full; i++) {
            ttak_ghash_update_soft(y_acc, h_key, ctx->aad + (i * 16));
        }
        if (aad_rem) {
            uint8_t pad[16] = {0};
            memcpy(pad, ctx->aad + (aad_full * 16), aad_rem);
            ttak_ghash_update_soft(y_acc, h_key, pad);
        }
    }

    /* 3) J0 for 96-bit IV: J0 = IV || 0x00000001 */
    memcpy(j0, ctx->iv, 12);
    j0[12] = 0;
    j0[13] = 0;
    j0[14] = 0;
    j0[15] = 1;

    /* E(K, J0) */
    ttak_aes256_enc_block(j0_enc, j0, ctx);

    /* 4) AES-CTR encryption and GHASH of ciphertext */
    uint32_t counter = 2;
    const size_t full_blocks = d_len / 16;
    const size_t rem_bytes   = d_len % 16;

    for (size_t i = 0; i < full_blocks; i++) {
        uint8_t ctr_blk[16];
        uint8_t ks[16];

        memcpy(ctr_blk, ctx->iv, 12);
        const uint32_t be = ttak_bswap32(counter);
        memcpy(ctr_blk + 12, &be, 4);

        ttak_aes256_enc_block(ks, ctr_blk, ctx);

        for (int j = 0; j < 16; j++) {
            p_dst[i * 16 + (size_t)j] = (uint8_t)(p_src[i * 16 + (size_t)j] ^ ks[j]);
        }

        ttak_ghash_update_soft(y_acc, h_key, p_dst + (i * 16));
        counter++;
    }

    if (rem_bytes) {
        uint8_t ctr_blk[16];
        uint8_t ks[16];
        uint8_t cpad[16] = {0};

        memcpy(ctr_blk, ctx->iv, 12);
        const uint32_t be = ttak_bswap32(counter);
        memcpy(ctr_blk + 12, &be, 4);

        ttak_aes256_enc_block(ks, ctr_blk, ctx);

        for (size_t j = 0; j < rem_bytes; j++) {
            p_dst[full_blocks * 16 + j] = (uint8_t)(p_src[full_blocks * 16 + j] ^ ks[j]);
            cpad[j] = p_dst[full_blocks * 16 + j];
        }

        /* GHASH needs 16-byte blocks; pad the final ciphertext fragment with zeros. */
        ttak_ghash_update_soft(y_acc, h_key, cpad);
    }

    /* 5) GHASH lengths: [len(AAD)]_64 || [len(C)]_64 in bits, both big-endian */
    uint8_t len_blk[16];
    const uint64_t aad_bits = (uint64_t)ctx->aad_len * 8ull;
    const uint64_t c_bits   = (uint64_t)d_len * 8ull;
    ttak_store_be64(len_blk + 0, aad_bits);
    ttak_store_be64(len_blk + 8, c_bits);
    ttak_ghash_update_soft(y_acc, h_key, len_blk);

    /* 6) Tag = GHASH xor E(K, J0) */
    for (int i = 0; i < 16; i++) {
        ctx->tag[i] = (uint8_t)(y_acc[i] ^ j0_enc[i]);
    }

    return TTAK_IO_SUCCESS;
}

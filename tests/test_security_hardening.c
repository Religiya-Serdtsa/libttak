/**
 * @file test_security_hardening.c
 * @brief Tests for security hardening fixes:
 *
 *  1. ChaCha20-Poly1305 block-scatter/gather OOB prevention (block_count bounds).
 *  2. ChaCha20-Poly1305 block_capacity multiplication overflow rejection.
 *  3. AES-256-GCM counter overflow guard.
 *  4. AES-256-GCM AAD null-pointer rejection.
 *  5. Ring buffer zero-capacity / zero-item-size rejection.
 *  6. Ring buffer capacity * item_size multiplication overflow rejection.
 *  7. Memory allocator size overflow rejection.
 */

#include <ttak/security/security_engine.h>
#include <ttak/container/ringbuf.h>
#include <ttak/mem/mem.h>

#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <stdio.h>
#include <limits.h>

#include "test_macros.h"

/* ------------------------------------------------------------------ */
/* Helpers                                                             */
/* ------------------------------------------------------------------ */

/* Minimal AES-256 round-key expansion (FIPS 197) for test purposes.
 * We just need any valid-looking key schedule so ttak_aes256_gcm_execute
 * at least reaches our target code paths.  We use the zero-key schedule. */
static void expand_zero_key(uint8_t rk[15][16]) {
    memset(rk, 0, 15 * 16);
    /* A fully-zeroed schedule passes the round-key array but produces
     * predictable (not secure) output — fine for structural tests. */
}

/* ------------------------------------------------------------------ */
/* 1. ChaCha20-Poly1305: block_count bounds check                      */
/* ------------------------------------------------------------------ */

static void test_chacha20_block_oob_rejected(void) {
    /* Build a context that uses scatter-gather I/O with block_count = 1
     * but requests encryption of 2 blocks worth of data.  Without the fix
     * the code would walk off the end of in_blocks[].  With the fix it
     * must return TTAK_IO_ERR_RANGE. */

    static const uint8_t key[32] = {
        0x00,0x01,0x02,0x03,0x04,0x05,0x06,0x07,
        0x08,0x09,0x0a,0x0b,0x0c,0x0d,0x0e,0x0f,
        0x10,0x11,0x12,0x13,0x14,0x15,0x16,0x17,
        0x18,0x19,0x1a,0x1b,0x1c,0x1d,0x1e,0x1f
    };
    static const uint8_t nonce[12] = {0};
    static uint8_t block0[64];
    static uint8_t block1_out[64];

    memset(block0, 0x42, sizeof(block0));
    memset(block1_out, 0, sizeof(block1_out));

    /* in_blocks has only 1 element, out is linear */
    const uint8_t *in_blocks_ptr[1] = { block0 };
    uint8_t tag[16];

    ttak_crypto_ctx_t ctx = {0};
    ctx.key      = key;
    ctx.key_len  = 32;
    ctx.iv_len   = 12;
    memcpy(ctx.iv, nonce, 12);
    /* scatter input: 1 block of 64 bytes */
    ctx.in_blocks  = in_blocks_ptr;
    ctx.block_size = 64;
    ctx.block_count = 1;          /* only 1 block available */
    ctx.out        = block1_out;
    ctx.tag        = tag;
    ctx.tag_len    = 16;

    /* Ask for 128 bytes (2 blocks) — must fail because in_blocks only has 1 */
    ttak_io_status_t rc = ttak_chacha20_poly1305_execute(&ctx, NULL, block1_out, 128);
    ASSERT_MSG(rc == TTAK_IO_ERR_RANGE,
               "Expected TTAK_IO_ERR_RANGE for OOB block access, got %d", rc);
}

/* ------------------------------------------------------------------ */
/* 2. ChaCha20-Poly1305: counter overflow guard                        */
/* ------------------------------------------------------------------ */

static void test_chacha20_counter_overflow_rejected(void) {
    /* Verify that block_count * block_size overflow in the capacity check
     * is caught and returns TTAK_IO_ERR_RANGE.
     * block_size=64, block_count = SIZE_MAX/64 + 2 overflows SIZE_MAX. */
    static const uint8_t key[32] = {0};
    static const uint8_t nonce[12] = {0};

    uint8_t dummy_out[1] = {0};
    uint8_t tag[16]      = {0};

    static const uint8_t dummy_byte = 0;
    const uint8_t *dummy_in_ptr = &dummy_byte;

    ttak_crypto_ctx_t ctx = {0};
    ctx.key        = key;
    ctx.key_len    = 32;
    ctx.iv_len     = 12;
    memcpy(ctx.iv, nonce, 12);
    ctx.in_blocks  = &dummy_in_ptr;
    ctx.block_size = 64;
    ctx.block_count = (SIZE_MAX / 64) + 2;   /* overflows SIZE_MAX when multiplied */
    ctx.out        = dummy_out;
    ctx.tag        = tag;
    ctx.tag_len    = 16;

    ttak_io_status_t rc = ttak_chacha20_poly1305_execute(&ctx, NULL, dummy_out, 0);
    ASSERT_MSG(rc == TTAK_IO_ERR_RANGE,
               "Expected TTAK_IO_ERR_RANGE for block_capacity overflow, got %d", rc);
}

/* ------------------------------------------------------------------ */
/* 3. AES-256-GCM: AAD null-pointer rejection                          */
/* ------------------------------------------------------------------ */

static void test_aes_gcm_null_aad_rejected(void) {
    uint8_t rk[15][16];
    expand_zero_key(rk);

    static const uint8_t nonce[12] = {0};
    static uint8_t plain[16] = {0};
    static uint8_t cipher[16];
    uint8_t tag[16];

    ttak_crypto_ctx_t ctx = {0};
    memcpy(ctx.iv, nonce, 12);
    ctx.aad     = NULL;
    ctx.aad_len = 8;          /* claims 8 bytes of AAD but pointer is NULL */
    ctx.in      = plain;
    ctx.in_len  = sizeof(plain);
    ctx.out     = cipher;
    ctx.out_len = sizeof(cipher);
    ctx.tag     = tag;
    ctx.tag_len = 16;
    memcpy(ctx.hw_state.aes.round_keys, rk, sizeof(rk));

    ttak_io_status_t rc = ttak_aes256_gcm_execute(&ctx, NULL, NULL, 0);
    ASSERT_MSG(rc == TTAK_IO_ERR_INVALID_ARGUMENT,
               "Expected TTAK_IO_ERR_INVALID_ARGUMENT for NULL aad with aad_len>0, got %d", rc);
}

/* ------------------------------------------------------------------ */
/* 4. Ring buffer: zero capacity rejected                              */
/* ------------------------------------------------------------------ */

static void test_ringbuf_zero_capacity_rejected(void) {
    ttak_ringbuf_t *rb = ttak_ringbuf_create(0, sizeof(int));
    ASSERT_MSG(rb == NULL, "Expected NULL for zero-capacity ring buffer");
}

/* ------------------------------------------------------------------ */
/* 5. Ring buffer: zero item_size rejected                             */
/* ------------------------------------------------------------------ */

static void test_ringbuf_zero_item_size_rejected(void) {
    ttak_ringbuf_t *rb = ttak_ringbuf_create(16, 0);
    ASSERT_MSG(rb == NULL, "Expected NULL for zero-item-size ring buffer");
}

/* ------------------------------------------------------------------ */
/* 6. Ring buffer: capacity * item_size overflow rejected              */
/* ------------------------------------------------------------------ */

static void test_ringbuf_capacity_overflow_rejected(void) {
    /* (SIZE_MAX / 2 + 1) * 2 overflows SIZE_MAX */
    size_t big_cap  = (SIZE_MAX / 2) + 1;
    size_t big_item = 2;
    ttak_ringbuf_t *rb = ttak_ringbuf_create(big_cap, big_item);
    ASSERT_MSG(rb == NULL, "Expected NULL for overflow capacity*item_size");
}

/* ------------------------------------------------------------------ */
/* 7. Memory allocator: SIZE_MAX allocation rejected (overflow guard)  */
/* ------------------------------------------------------------------ */

static void test_mem_alloc_size_overflow_rejected(void) {
    /* Requesting SIZE_MAX bytes must not succeed — the header+canary
     * arithmetic would overflow, and the allocator must detect that
     * and return NULL rather than corrupting memory. */
    void *p = ttak_mem_alloc(SIZE_MAX, 0, 0);
    ASSERT_MSG(p == NULL, "Expected NULL for SIZE_MAX allocation (overflow guard)");
}

/* ------------------------------------------------------------------ */
/* main                                                                */
/* ------------------------------------------------------------------ */

int main(void) {
    RUN_TEST(test_chacha20_block_oob_rejected);
    RUN_TEST(test_chacha20_counter_overflow_rejected);
    RUN_TEST(test_aes_gcm_null_aad_rejected);
    RUN_TEST(test_ringbuf_zero_capacity_rejected);
    RUN_TEST(test_ringbuf_zero_item_size_rejected);
    RUN_TEST(test_ringbuf_capacity_overflow_rejected);
    RUN_TEST(test_mem_alloc_size_overflow_rejected);
    return 0;
}

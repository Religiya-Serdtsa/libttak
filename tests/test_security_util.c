/**
 * @file test_security_util.c
 * @brief Tests for security helpers and zeroing routines.
 */

#include <ttak/security/security_util.h>
#include <ttak/security/aria.h>
#include <ttak/security/lsh.h>

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "test_macros.h"

static void test_secure_zero_clears_buffer(void) {
    uint8_t buf[64];
    memset(buf, 0xab, sizeof(buf));
    ttak_secure_zero(buf, sizeof(buf));

    for (size_t i = 0; i < sizeof(buf); ++i) {
        ASSERT(buf[i] == 0);
    }
}

static void test_secure_zero_null_noop(void) {
    ttak_secure_zero(NULL, 64);
    ttak_secure_zero((void *)1, 0);
}

static void test_aria_schedule_wipe(void) {
    static const uint8_t key[16] = {
        0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
        0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f
    };
    ttak_aria_schedule_t sched;

    ASSERT(ttak_aria_schedule_init(&sched, key, sizeof(key)) != 0);
    ASSERT(memcmp(sched.enc_keys, key, sizeof(key)) != 0 || sched.rounds != 0);

    ttak_aria_schedule_wipe(&sched);
    uint8_t zero[sizeof(sched)] = {0};
    ASSERT(memcmp(&sched, zero, sizeof(sched)) == 0);

    ttak_aria_schedule_wipe(NULL);
}

static void test_lsh_wipe(void) {
    ttak_lsh_ctx_t ctx;
    uint8_t hash[TTAK_LSH256_DIGEST_SIZE];

    ASSERT(ttak_lsh_init(&ctx, TTAK_LSH_256) == 0);
    ttak_lsh_update(&ctx, (const uint8_t *)"hello", 5);
    ttak_lsh_final(&ctx, hash);

    /* After final the context has already been wiped, but calling wipe
       again must be safe and a no-op. */
    ttak_lsh_wipe(&ctx);
    ttak_lsh_wipe(NULL);
}

int main(void) {
    RUN_TEST(test_secure_zero_clears_buffer);
    RUN_TEST(test_secure_zero_null_noop);
    RUN_TEST(test_aria_schedule_wipe);
    RUN_TEST(test_lsh_wipe);
    return 0;
}

/**
 * @file test_aria.c
 * @brief Basic ARIA block-cipher tests using KISA reference vectors.
 */

#include <stdio.h>
#include <stdint.h>
#include <string.h>

#include <ttak/security/aria.h>

static int test_vector_192(void) {
    const uint8_t key[24] = {
        0x00, 0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77,
        0x88, 0x99, 0xaa, 0xbb, 0xcc, 0xdd, 0xee, 0xff,
        0x00, 0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77
    };
    const uint8_t pt[16] = {
        0x11, 0x11, 0x11, 0x11, 0xaa, 0xaa, 0xaa, 0xaa,
        0x11, 0x11, 0x11, 0x11, 0xbb, 0xbb, 0xbb, 0xbb
    };
    const uint8_t expected[16] = {
        0x8d, 0x14, 0x70, 0x62, 0x5f, 0x59, 0xeb, 0xac,
        0xb0, 0xe5, 0x5b, 0x53, 0x4b, 0x3e, 0x46, 0x2b
    };

    ttak_aria_schedule_t sched;
    if (ttak_aria_schedule_init(&sched, key, sizeof(key)) != 14) {
        printf("FAIL: ARIA-192 schedule init\n");
        return 1;
    }

    uint8_t ct[16];
    ttak_aria_encrypt_block(ct, pt, &sched);
    if (memcmp(ct, expected, sizeof(expected)) != 0) {
        printf("FAIL: ARIA-192 encryption mismatch\n");
        return 1;
    }

    uint8_t decrypted[16];
    ttak_aria_decrypt_block(decrypted, ct, &sched);
    if (memcmp(decrypted, pt, sizeof(pt)) != 0) {
        printf("FAIL: ARIA-192 decryption mismatch\n");
        return 1;
    }

    return 0;
}

static int test_vector_256_roundtrip(void) {
    const uint8_t key[32] = {0};
    const uint8_t pt[16] = {0};

    ttak_aria_schedule_t sched;
    if (ttak_aria_schedule_init(&sched, key, sizeof(key)) != 16) {
        printf("FAIL: ARIA-256 schedule init\n");
        return 1;
    }

    uint8_t ct[16];
    ttak_aria_encrypt_block(ct, pt, &sched);

    uint8_t decrypted[16];
    ttak_aria_decrypt_block(decrypted, ct, &sched);
    if (memcmp(decrypted, pt, sizeof(pt)) != 0) {
        printf("FAIL: ARIA-256 roundtrip mismatch\n");
        return 1;
    }

    return 0;
}

static int test_vector_128_roundtrip(void) {
    const uint8_t key[16] = {
        0x00, 0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77,
        0x88, 0x99, 0xaa, 0xbb, 0xcc, 0xdd, 0xee, 0xff
    };
    const uint8_t pt[16] = {
        0x00, 0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77,
        0x88, 0x99, 0xaa, 0xbb, 0xcc, 0xdd, 0xee, 0xff
    };

    ttak_aria_schedule_t sched;
    if (ttak_aria_schedule_init(&sched, key, sizeof(key)) != 12) {
        printf("FAIL: ARIA-128 schedule init\n");
        return 1;
    }

    uint8_t ct[16];
    ttak_aria_encrypt_block(ct, pt, &sched);

    uint8_t decrypted[16];
    ttak_aria_decrypt_block(decrypted, ct, &sched);
    if (memcmp(decrypted, pt, sizeof(pt)) != 0) {
        printf("FAIL: ARIA-128 roundtrip mismatch\n");
        return 1;
    }

    return 0;
}

int main(void) {
    int failures = 0;
    failures += test_vector_192();
    failures += test_vector_256_roundtrip();
    failures += test_vector_128_roundtrip();
    if (failures == 0) {
        printf("ARIA tests passed.\n");
    }
    return failures;
}

#include <ttak/security/siphash.h>

#define ROTL(x, b) (uint64_t)(((x) << (b)) | ((x) >> (64 - (b))))

#define U8TO64_LE(p) \
    (((uint64_t)((p)[0])) | \
    ((uint64_t)((p)[1]) << 8) | \
    ((uint64_t)((p)[2]) << 16) | \
    ((uint64_t)((p)[3]) << 24) | \
    ((uint64_t)((p)[4]) << 32) | \
    ((uint64_t)((p)[5]) << 40) | \
    ((uint64_t)((p)[6]) << 48) | \
    ((uint64_t)((p)[7]) << 56))

#define SIPROUND \
    do {                    \
        v0 += v1;           \
        v1 = ROTL(v1, 13);  \
        v1 ^= v0;           \
        v0 = ROTL(v0, 32);  \
        v2 += v3;           \
        v3 = ROTL(v3, 16);  \
        v3 ^= v2;           \
        v0 += v3;           \
        v3 = ROTL(v3, 21);  \
        v3 ^= v0;           \
        v2 += v1;           \
        v1 = ROTL(v1, 17);  \
        v1 ^= v2;           \
        v2 = ROTL(v2, 32);  \
    } while (0)

uint64_t ttak_siphash24(const void *key, size_t len, uint64_t k0, uint64_t k1) {
    uint64_t v0 = 0x736f6d6570736575ULL ^ k0;
    uint64_t v1 = 0x646f72616e646f6dULL ^ k1;
    uint64_t v2 = 0x6c7967656e657261ULL ^ k0;
    uint64_t v3 = 0x7465646279746573ULL ^ k1;
    const uint8_t *data = (const uint8_t *)key;
    const uint8_t *end = data + (len - (len % 8));
    uint64_t m;

    for (; data != end; data += 8) {
        m = U8TO64_LE(data);
        v3 ^= m;
        SIPROUND;
        SIPROUND;
        v0 ^= m;
    }

    const uint8_t *left = data;
    uint64_t b = ((uint64_t)len) << 56;
    switch (len % 8) {
        case 7: b |= ((uint64_t)left[6]) << 48; // fallthrough
        case 6: b |= ((uint64_t)left[5]) << 40; // fallthrough
        case 5: b |= ((uint64_t)left[4]) << 32; // fallthrough
        case 4: b |= ((uint64_t)left[3]) << 24; // fallthrough
        case 3: b |= ((uint64_t)left[2]) << 16; // fallthrough
        case 2: b |= ((uint64_t)left[1]) << 8;  // fallthrough
        case 1: b |= ((uint64_t)left[0]); break;
        case 0: break;
    }

    v3 ^= b;
    SIPROUND;
    SIPROUND;
    v0 ^= b;

    v2 ^= 0xff;
    SIPROUND;
    SIPROUND;
    SIPROUND;
    SIPROUND;

    return v0 ^ v1 ^ v2 ^ v3;
}

uint64_t ttak_siphash24_u64(uint64_t val, uint64_t k0, uint64_t k1) {
    uint64_t v0 = 0x736f6d6570736575ULL ^ k0;
    uint64_t v1 = 0x646f72616e646f6dULL ^ k1;
    uint64_t v2 = 0x6c7967656e657261ULL ^ k0;
    uint64_t v3 = 0x7465646279746573ULL ^ k1;
    uint64_t m = val;

    v3 ^= m;
    SIPROUND;
    SIPROUND;
    v0 ^= m;

    v2 ^= 0xff;
    SIPROUND;
    SIPROUND;
    SIPROUND;
    SIPROUND;

    return v0 ^ v1 ^ v2 ^ v3;
}

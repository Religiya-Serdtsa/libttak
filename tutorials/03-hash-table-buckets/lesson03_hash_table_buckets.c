#define _XOPEN_SOURCE 700
#include <inttypes.h>
#include <stdio.h>
#include <ttak/ht/hash.h>

static void demo_hash(uintptr_t key, uint64_t k0, uint64_t k1) {
    uint64_t hash = gen_hash_sip24(key, k0, k1);
    printf("key=0x%" PRIXPTR " hash=0x%" PRIX64 "\n", key, hash);
}

int main(void) {
    demo_hash(0xDEADBEEFu, 0xA55A55A5ULL, 0x12345678ULL);
    demo_hash(42u, 0xCAFEBABEULL, 0x0BADF00DULL);
    puts("Lesson 03 focuses on bucket control bytes and probing order.");
    return 0;
}

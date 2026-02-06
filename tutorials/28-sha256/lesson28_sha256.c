#define _XOPEN_SOURCE 700
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <ttak/security/sha256.h>

int main(void) {
    const char *msg = "libttak";
    uint8_t digest[SHA256_BLOCK_SIZE];
    SHA256_CTX ctx;
    sha256_init(&ctx);
    sha256_update(&ctx, (const uint8_t *)msg, strlen(msg));
    sha256_final(&ctx, digest);
    printf("sha256(libttak)=");
    for (size_t i = 0; i < SHA256_BLOCK_SIZE; ++i) {
        printf("%02x", digest[i]);
    }
    putchar('
');
    return 0;
}

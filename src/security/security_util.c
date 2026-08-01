/**
 * @file security_util.c
 * @brief Security helper implementations.
 */

#include <ttak/security/security_util.h>

void ttak_secure_zero(void *ptr, size_t len) {
    if (!ptr || len == 0) {
        return;
    }

    volatile unsigned char *p = (volatile unsigned char *)ptr;
    while (len-- > 0) {
        *p++ = 0;
    }
}

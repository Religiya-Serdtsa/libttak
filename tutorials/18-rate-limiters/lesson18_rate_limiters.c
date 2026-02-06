#define _XOPEN_SOURCE 700
#include <stdio.h>
#include <ttak/limit/limit.h>

int main(void) {
    ttak_ratelimit_t rl;
    ttak_ratelimit_init(&rl, 5.0, 2.0);
    for (int i = 0; i < 5; ++i) {
        printf("request %d -> %s\n", i, ttak_ratelimit_allow(&rl) ? "allowed" : "denied");
    }
    return 0;
}

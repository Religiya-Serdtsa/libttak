#define _XOPEN_SOURCE 700
#include <stdio.h>
#include <ttak/mem/owner.h>
#include <ttak/unsafe/context.h>

static void bridge_cb(void *shared_mem, size_t shared_size, void *arg) {
    (void)shared_size;
    int *counter = (int *)shared_mem;
    (*counter)++;
    printf("bridge %s -> counter=%d\n", (const char *)arg, *counter);
}

int main(void) {
    ttak_owner_t *first = ttak_owner_create(TTAK_OWNER_SAFE_DEFAULT);
    ttak_owner_t *second = ttak_owner_create(TTAK_OWNER_SAFE_DEFAULT);
    if (!first || !second) {
        fputs("owner creation failed\n", stderr);
        return 1;
    }

    int shared_value = 0;
    ttak_context_t ctx;
    if (ttak_context_init(&ctx, first, second, &shared_value, sizeof(shared_value), __TTAK_CTX_USE_FIRST__)) {
        ttak_context_run(&ctx, __TTAK_CTX_USE_FIRST__, bridge_cb, (void *)"first");
        ttak_context_run(&ctx, __TTAK_CTX_USE_SECOND__, bridge_cb, (void *)"second");
        ttak_context_destroy(&ctx);
    }
    ttak_owner_destroy(second);
    ttak_owner_destroy(first);
    return 0;
}

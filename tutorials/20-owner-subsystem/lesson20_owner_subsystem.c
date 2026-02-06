#define _XOPEN_SOURCE 700
#include <stdio.h>
#include <ttak/mem/owner.h>

static void greet(void *ctx, void *args) {
    const char *name = ctx ? (const char *)ctx : "owner";
    const char *msg = args ? (const char *)args : "hello";
    printf("%s -> %s\n", name, msg);
}

int main(void) {
    ttak_owner_t *owner = ttak_owner_create(TTAK_OWNER_SAFE_DEFAULT);
    if (!owner) {
        fputs("owner allocation failed\n", stderr);
        return 1;
    }

    ttak_owner_register_resource(owner, "name", (void *)"LibTTAK owner");
    ttak_owner_register_func(owner, "greet", greet);
    ttak_owner_execute(owner, "greet", "name", (void *)"sandbox hello");
    ttak_owner_destroy(owner);
    return 0;
}

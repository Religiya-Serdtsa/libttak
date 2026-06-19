#ifndef _GNU_SOURCE
#  define _GNU_SOURCE
#endif
#ifndef _XOPEN_SOURCE
#  define _XOPEN_SOURCE 700
#endif

#include <stdio.h>
#include <string.h>
#include <ttak/mem/mem.h>
#include <ttak/timing/timing.h>
#include <ttak/mem/owner.h>
#include <ttak/mem/epoch_gc.h>

static void greet(void *ctx, void *args) {
    const char *name = ctx ? (const char *)ctx : "owner";
    const char *msg = args ? (const char *)args : "hello";
    printf("  greet: %s -> %s\n", name, msg);
}

static void show_resource(void *ctx, void *args) {
    (void)args;
    if (ctx) {
        printf("  owner sees resource: '%s'\n", (const char *)ctx);
    } else {
        printf("  owner does NOT see the resource (it was unuse'd)\n");
    }
}

int main(void) {
    ttak_epoch_gc_t gc;
    ttak_owner_t *owner = ttak_owner_create(TTAK_OWNER_SAFE_DEFAULT);
    if (!owner) {
        fputs("owner allocation failed\n", stderr);
        return 1;
    }

    ttak_epoch_gc_init(&gc);

    /* Allocate a GC-managed block and register it under the owner. */
    char *owner_name = ttak_fastalloc(&gc, 128, TT_MILLI_SECOND(10), ttak_get_tick_count());
    if (!owner_name) {
        fputs("allocation failed\n", stderr);
        return 1;
    }

    printf("Enter a name: ");
    if (scanf("%127s", owner_name) != 1) {
        strcpy(owner_name, "anonymous");
    }

    ttak_owner_register_resource(owner, "name", owner_name);
    ttak_owner_register_func(owner, "greet", greet);
    ttak_owner_register_func(owner, "show", show_resource);

    printf("\n1. BEFORE ttak_mem_unuse:\n");
    ttak_owner_execute(owner, "show", "name", NULL);
    ttak_owner_execute(owner, "greet", "name", (void *)"sandbox hello");

    printf("\n2. CALLING ttak_mem_unuse(owner_name, owner):\n");
    ttak_mem_unuse(owner_name, owner);

    printf("\n3. AFTER ttak_mem_unuse:\n");
    ttak_owner_execute(owner, "show", "name", NULL);
    ttak_owner_execute(owner, "greet", "name", (void *)"still there?");

    printf("\n4. Direct pointer is still valid (unuse does not free immediately):\n");
    printf("  direct access: '%s'\n", owner_name);

    printf("\n5. Rotating epoch GC to collect the released block...\n");
    ttak_epoch_gc_rotate(&gc);
    printf("  GC rotation done.\n");
    printf("  direct access: '%s'\n", owner_name);

    printf("\n6. Cleanup owner + GC.\n");
    ttak_owner_destroy(owner);
    ttak_epoch_gc_destroy(&gc);
    return 0;
}

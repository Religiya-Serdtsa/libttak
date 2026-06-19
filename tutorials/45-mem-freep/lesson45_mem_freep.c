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

int main(void) {
    uint64_t now = ttak_get_tick_count();

    /* Allocate two buffers to compare ttak_mem_free vs ttak_mem_freep. */
    char *buffer_a = ttak_mem_alloc_raw(64, TT_SECOND(1), now);
    char *buffer_b = ttak_mem_alloc_raw(64, TT_SECOND(1), now);
    if (!buffer_a || !buffer_b) {
        fputs("allocation failed\n", stderr);
        return 1;
    }

    strcpy(buffer_a, "hello");
    strcpy(buffer_b, "world");

    printf("initial values:\n");
    printf("  buffer_a = '%s' (%p)\n", buffer_a, (void *)buffer_a);
    printf("  buffer_b = '%s' (%p)\n", buffer_b, (void *)buffer_b);

    printf("\n1. ttak_mem_free(buffer_a): pointer keeps its old address\n");
    ttak_mem_free(buffer_a);
    printf("  buffer_a = %p  (dangling - do not dereference!)\n", (void *)buffer_a);

    printf("\n2. ttak_mem_freep(&buffer_b): pointer is freed and zeroed\n");
    ttak_mem_freep((void **)&buffer_b);
    printf("  buffer_b = %p  (NULL, safe to double-free)\n", (void *)buffer_b);

    printf("\n3. double free through ttak_mem_freep is a no-op\n");
    ttak_mem_freep((void **)&buffer_b);
    printf("  still safe, buffer_b remains NULL\n");

    printf("\n4. ttak_mem_freep(NULL) and ttak_mem_freep(&NULL_ptr) are also safe\n");
    ttak_mem_freep(NULL);
    void *null_ptr = NULL;
    ttak_mem_freep(&null_ptr);
    printf("  no crash\n");

    /* buffer_a is dangling; we deliberately do NOT touch it again. */
    (void)buffer_a;
    return 0;
}

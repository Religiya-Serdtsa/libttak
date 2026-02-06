#define _XOPEN_SOURCE 700
#include <stdio.h>
#include <ttak/container/ringbuf.h>

int main(void) {
    ttak_ringbuf_t *rb = ttak_ringbuf_create(3, sizeof(int));
    if (!rb) {
        fputs("ring buffer init failed\n", stderr);
        return 1;
    }

    for (int i = 0; i < 4; ++i) {
        if (!ttak_ringbuf_push(rb, &i)) {
            printf("ring buffer full at %d\n", i);
        }
    }

    int value = 0;
    while (ttak_ringbuf_pop(rb, &value)) {
        printf("popped %d\n", value);
    }

    ttak_ringbuf_destroy(rb);
    return 0;
}

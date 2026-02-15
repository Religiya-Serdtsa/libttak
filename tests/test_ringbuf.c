#include <ttak/container/ringbuf.h>
#include "test_macros.h"

static void test_ringbuf_push_pop_cycle(void) {
    ttak_ringbuf_t *rb = ttak_ringbuf_create(5, sizeof(int));
    int in = 10;
    int out = 0;

    ASSERT(ttak_ringbuf_is_empty(rb));
    ttak_ringbuf_push(rb, &in);
    ASSERT(ttak_ringbuf_count(rb) == 1);
    ttak_ringbuf_pop(rb, &out);
    ASSERT(out == in);
    ASSERT(ttak_ringbuf_is_empty(rb));

    for (int i = 0; i < 5; ++i) {
        ttak_ringbuf_push(rb, &i);
    }
    ASSERT(ttak_ringbuf_is_full(rb));
    ASSERT(ttak_ringbuf_push(rb, &in) == false);

    ttak_ringbuf_destroy(rb);
}

int main(void) {
    RUN_TEST(test_ringbuf_push_pop_cycle);
    return 0;
}

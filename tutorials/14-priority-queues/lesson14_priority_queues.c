#include <stdint.h>
#include <stdio.h>
#include <ttak/priority/internal/queue.h>
#include <ttak/timing/timing.h>

int main(void) {
    struct __internal_ttak_proc_priority_queue_t queue = {0};
    ttak_priority_queue_init(&queue);
    queue.init(&queue);

    uint64_t now = ttak_get_tick_count();
    ttak_task_t *fake_task = (ttak_task_t *)(uintptr_t)0x1;
    queue.push(&queue, fake_task, 0, now);
    printf("queue size after push: %zu\n", queue.get_size(&queue));
    queue.pop(&queue, now);
    printf("queue empty? %s\n", queue.get_size(&queue) == 0 ? "yes" : "no");
    return 0;
}

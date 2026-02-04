# Tutorial 14 – Priority Queues

[`modules/14-priority-queues`](../modules/14-priority-queues/README.md) digs into `src/priority/queue.c` and the heap helpers that order tasks.

`lesson14_priority_queues.c` hits the internal queue vtable so you can watch pushes/pops; rebuild it whenever you tweak heap semantics.

## Checklist

1. Restate how `ttak_priority_queue_init`, push/pop, and `get_size` should behave for both normal and "nice" scheduling modes.
2. Implement the heap operations (`src/priority/heap.c`) alongside the queue wrapper so the async runtime has deterministic ordering.
3. Run `make tests/test_priority` plus any targeted benchmarks you script in this folder to validate ordering guarantees.
4. Capture diagrams that show how tasks bubble through the heap so Lessons 15–16 build on the same mental model.

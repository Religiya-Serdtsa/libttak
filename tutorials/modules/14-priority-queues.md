# 14 – Priority Queues

**Focus:** Clone the heap + queue pair that schedules async work.

**Source material:**
- `src/priority/queue.c`
- `src/priority/heap.c`
- `tests/priority/test_heap.c`

## Steps
1. Rebuild the binary heap backing store with the same comparator semantics.
1. Copy push/pop/sift operations paying attention to how priorities are compared.
1. Recreate the queue wrapper that adds thread-safety or worker awareness.
1. Test with random inserts to ensure heap property never breaks.

## Checks
- Heap tests pass and priority order matches upstream logs.
- Queue wrapper exposes the same blocking APIs as documented.

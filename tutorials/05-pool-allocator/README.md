# Tutorial 05 – Pool Allocator

This workspace accompanies [`modules/05-pool-allocator`](../modules/05-pool-allocator/README.md) and focuses on rebuilding the bitmap-backed object pool under `src/container/pool.c`.

Use `lesson05_pool_allocator.c` plus the provided Makefile as a harness for allocating/freeing objects while you match the upstream locking and accounting rules.

## Checklist

1. Re-read the lesson with `include/ttak/container/pool.h` nearby so that the struct layout and public signatures are clear.
2. Document how the bitmap scan, `used_count`, and spinlock interplay—especially what happens when the pool is full or a caller frees an invalid pointer.
3. Mirror `ttak_object_pool_create/alloc/free/destroy` inside `src/container/pool.c`, then run `make tests/test_new_features` to execute the pool regression in `tests/test_new_features.c`.
4. Capture allocator traces (or screenshots of your scratch logs) in this folder along with your final takeaways.

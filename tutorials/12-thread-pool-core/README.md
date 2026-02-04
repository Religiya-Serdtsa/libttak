# Tutorial 12 – Thread Pool Core

[`modules/12-thread-pool-core`](../modules/12-thread-pool-core/README.md) has you rebuild the pool lifecycle in `src/thread/pool.c`.

`lesson12_thread_pool_core.c` submits a tiny job to the pool and resolves a future—compile it to watch end-to-end scheduling as you iterate.

## Checklist

1. Write down the control flow for creating workers, submitting tasks, and tearing the pool down (queues, futures, timers).
2. Clone the pool allocation, submit path, and shutdown logic along with the helper functions that wire promises/futures together.
3. Use `make tests/test_thread` plus `make tests/test_async` to ensure the pool cooperates with the runtime.
4. Record deadlock scenarios you encounter here so they inform the follow-up lessons on workers and scheduling.

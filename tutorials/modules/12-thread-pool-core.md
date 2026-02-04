# 12 – Thread Pool Core

**Focus:** Replicate the pool initialization and job submission plumbing.

**Source material:**
- `src/thread/pool.c`
- `src/thread/worker.c`
- `include/ttak/threadpool.h`

## Steps
1. Clone the pool struct layout, focusing on how workers store queues and stop flags.
1. Copy `ttak_threadpool_create`/`destroy`, matching error paths and cleanup order.
1. Implement the submit function so tasks land in worker queues with the same ownership semantics.
1. Write a demo that enqueues CPU + IO style jobs and prints when they complete.

## Checks
- Pool spins up all workers and shuts them down cleanly on destroy.
- Demo run executes jobs in roughly FIFO order.

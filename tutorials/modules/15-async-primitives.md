# 15 – Async Primitives

**Focus:** Clone promises, futures, and tasks so asynchronous work can complete end-to-end.

**Source material:**
- `src/async/task.c`
- `src/async/promise.c`
- `src/async/future.c`

## Steps
1. Recreate `ttak_task_run` and how it hands ownership to promises/futures.
1. Clone the promise state machine exactly.
1. Implement future waiting semantics, matching condition variable usage.
1. Hook futures into a toy app that spawns multiple promises and waits for completion.

## Checks
- Promises resolve futures exactly once.
- Waiting on a future obeys timeout semantics identical to upstream.

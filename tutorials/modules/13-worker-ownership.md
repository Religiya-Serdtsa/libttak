# 13 – Worker Ownership

**Focus:** Understand and clone the worker lifecycle plus `ttak_owner_t` callbacks.

**Source material:**
- `src/thread/worker.c`
- `src/async/task.c`

## Steps
1. Trace how workers grab tasks, set the current owner, and release resources.
1. Recreate the callback chain triggered when a job finishes.
1. Implement cancellation hooks if the original supports them.
1. Add instrumentation prints for owner transitions while testing.

## Checks
- Owner callbacks fire in the same order as the reference implementation.
- Cancellation behaves like upstream (no leaked promises).

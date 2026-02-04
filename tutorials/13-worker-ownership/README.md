# Tutorial 13 – Worker Ownership

This folder lines up with [`modules/13-worker-ownership`](../modules/13-worker-ownership/README.md) and focuses on the glue in `src/thread/worker.c` that hands work units to owners.

`lesson13_worker_ownership.c` builds a `ttak_worker_wrapper_t` so you can trace the metadata that travels with each job.

## Checklist

1. Diagram how wrappers move between the pool, priority queues, and owner subsystem while keeping timestamps and nice values intact.
2. Mirror the worker init, dispatch, and completion helpers so they respect the ownership guarantees defined in Lesson 20.
3. Run both `make tests/test_thread` and `make tests/test_owner_complex` after major changes to confirm the worker lifecycle still honors owners.
4. Document any invariants you enforce (e.g., promise always resolved exactly once) for future reference.

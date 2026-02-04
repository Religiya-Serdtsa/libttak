# 16 – Async Scheduler

**Focus:** Wire the async primitives into schedulers and ensure tasks run in priority order.

**Source material:**
- `src/async/sched.c`
- `src/priority/scheduler.c`

## Steps
1. Clone scheduler initialization plus its connection to thread pools.
1. Implement tick/update loops that pull from heaps and wake workers.
1. Add instrumentation to confirm fairness between priorities.
1. Run integration tests tying together tasks, promises, and the scheduler.

## Checks
- Scheduler drains queues deterministically for reproducible tests.
- Instrumentation proves tasks of equal priority share CPU time.

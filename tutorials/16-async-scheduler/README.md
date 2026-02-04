# Tutorial 16 – Async Scheduler

This workspace ties into [`modules/16-async-scheduler`](../modules/16-async-scheduler/README.md) and focuses on `src/async/sched.c` plus the priority scheduler glue.

`lesson16_async_scheduler.c` spins up the async runtime and schedules a single task; use it to watch the queue + worker interplay.

## Checklist

1. Map out the scheduler state machine (init, schedule, yield, shutdown) and document how it coordinates with the thread pool.
2. Mirror the APIs in `ttak_async_*` and the priority scheduler callbacks so tasks advance correctly through ready/waiting queues.
3. Run `make tests/test_smart_sched` and `make tests/test_async` whenever you change scheduler code.
4. Capture logs from `lesson16_async_scheduler.c` showing workers draining the queue, then file them here as study material.

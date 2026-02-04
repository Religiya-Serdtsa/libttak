# Tutorial 15 – Async Primitives

[`modules/15-async-primitives`](../modules/15-async-primitives/README.md) covers tasks, promises, and futures under `src/async/{task,promise,future}.c`.

`lesson15_async_primitives.c` wires a promise/future pair; compile it to confirm the signaling path works as you re-create the internals.

## Checklist

1. List the lifecycle steps for promises/futures (creation, fulfillment, cancellation) and keep the notes next to this README.
2. Clone the `ttak_promise_*` and `ttak_future_*` helpers plus the task structs that feed them, matching the thread-safety guarantees in the lesson.
3. Execute `make tests/test_async` frequently to catch regressions across await/resume paths.
4. Record any race conditions you uncover so the scheduler work in Lesson 16 can avoid them.

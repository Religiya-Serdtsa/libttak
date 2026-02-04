# Tutorial 35 – Context Bridge

[`modules/35-context-bridge`](../modules/35-context-bridge/README.md) closes the tutorial path by rebuilding the shared-memory bridge in `src/unsafe/context.c` and `include/ttak/unsafe/context.h`.

`lesson35_context_bridge.c` initializes a context between two owners; recompile it every time you adjust bridge state machines.

## Checklist

1. Map out the state transitions for `ttak_context_t` (init, run, destroy) and how they enforce one-writer-at-a-time ownership.
2. Clone the bridge implementation and macros while confirming that every code path honors the lock ordering spelled out in the unsafe manual.
3. Use the lesson driver (and, if possible, integration tests) to exercise both owners swapping control with shared memory mutations.
4. Write the final "What I learned" entry summarizing how unsafe ownership differs from the safe subsystems.

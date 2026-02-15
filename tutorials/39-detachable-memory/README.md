# Lesson 39: Detachable Memory & Signal Guards

This lesson covers the detachable memory allocator that arrived with the new proposal. You will wire a detachable context, exercise the tiny-chunk cache, watch the arena rows advance, and enable the hard-kill signal guards that flush everything before the process dies.

## Objectives

1. Clone the reference implementation from `include/ttak/mem/detachable.h`, `src/mem/detachable.c`, and the safety helper in `internal/tt_jmp.h` to understand how detachable arenas layer on top of epoch reclamation.
2. Practice mixing context flags (`TTAK_ARENA_HAS_EPOCH_RECLAMATION`, `TTAK_ARENA_IS_URGENT_TASK`, etc.) so you can decide when caching vs. immediate retirement is appropriate.
3. Register a hard-kill handler using `ttak_hard_kill_graceful_exit` and verify that cached blocks are flushed when a signal trips.

> **Blueprint reference:** See `blueprints/10_detachable_memory.puml` for a structural overview of the cache, arena rows, and signal hooks.

## Repo Workspace

Use this folder as your scratch space:

```
tutorials/39-detachable-memory/
├── Makefile
├── README.md  ← you are here
└── lesson39_detachable_memory.c
```

`lesson39_detachable_memory.c` ships a runnable demo that:

- Spawns worker threads that allocate and free detachable chunks of different sizes.
- Shows when allocations reuse the cache vs. push pointers through the arena rows.
- Forks a short-lived helper process to demonstrate how `ttak_hard_kill_graceful_exit` flushes the registry and exits with the requested status code.

## Build & Run

```bash
make
./lesson39_detachable_memory
```

Expected output (abbreviated):

```
[main] launching detachable workers (cache hits follow)
worker[0] allocated 8 bytes (cache)
...
[stats] cache hits=120 misses=8 flushes=8
[signal-demo] child exited after SIGUSR1 with code 12
```

The exact numbers differ per run, but you should see cache hits climbing after the warm-up and the signal demo reporting the child exit code you configured.

## Verification Checklist

- [ ] `ttak_detachable_context_init` receives the mix of flags described above and initializes locks + cache.
- [ ] The worker threads log when allocations are cached vs. retired via epoch; 16-byte-or-less chunks should reuse the cache after a few iterations.
- [ ] The detachable registry is flushed in the signal demo (look for `[signal-demo] child exited ...`).
- [ ] `ttak_detachable_context_destroy` runs after the demo to release remaining rows and the cache.

Take notes on which flags you would toggle for latency-sensitive arenas vs. background maintenance arenas—this lesson is the new foundation for future detachable subsystems.

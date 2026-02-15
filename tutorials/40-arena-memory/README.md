# Lesson 40: Arena Memory Orchestration

This lesson focuses on the generational arena workflow built on top of `ttak_mem_alloc_safe`, the mem-tree tracker, and the epoch GC wrapper. You will practice how each arena generation is allocated, retired, and flushed without leaking, mirroring the pattern used by the lock-free cache benchmarks.

## Objectives

1. Clone the arena-related entry points inside `src/mem/mem.c`, `src/mem/epoch_gc.c`, and `src/mem_tree/mem_tree.c` so you understand how tracked allocations feed the tree and epochs.
2. Study how `bench/ttl-cache-multithread-bench/ttl_cache_bench_lockfree.c` seeds four arenas per epoch and why cache-line alignment matters for predictable RSS.
3. Build and run the sample program in this folder to verify that retiring an arena generation releases its mem-tree node and that `ttak_epoch_gc_rotate` frees the backing buffer.

> **Blueprint reference:** The arena + epoch interaction is illustrated in `blueprints/09_epoch_reclamation.puml`; use it alongside the README sections that describe the lock-free arena measurements.

## Repo Workspace

```
tutorials/40-arena-memory/
├── Makefile
├── README.md  ← you are here
└── lesson40_arena_memory.c
```

`lesson40_arena_memory.c` allocates fixed-width arena generations, uses `ttak_arena_generation_for_each` to carve deterministic chunks, retires each generation by releasing the mem-tree reference, and immediately rotates epochs to prove the buffer was flushed.

## Build & Run

```bash
make
./lesson40_arena_memory
```

Expected output (trimmed):

```
[arena] generation 0 started (capacity=4096, chunk=128)
  chunk[0] => 0x55f1e678c2a0 pattern=0x25
  chunk[8] => 0x55f1e678c340 pattern=0x2d
  used=4096 / 4096 bytes
  cleanup status: flushed
...
```

Exact addresses differ per run, but each generation should log deterministic chunk offsets and report `cleanup status: flushed` after the epoch rotates.

## Verification Checklist

- [ ] `ttak_mem_alloc_with_flags` is invoked with strict checks + cache alignment before every arena generation is registered with `ttak_epoch_gc_register`.
- [ ] Retiring a generation calls `ttak_mem_tree_find_node` + `ttak_mem_node_release` so the mem tree reports pressure before the next epoch rotation.
- [ ] `ttak_epoch_gc_rotate` reclaims each retired generation (look for the `cleanup status: flushed` log line).
- [ ] Translate what you learned back into the lock-free benchmark: align generation capacity/chunk size with the per-epoch arenas shown in `ttl_cache_bench_lockfree.c` and document the RSS impact in your notes.

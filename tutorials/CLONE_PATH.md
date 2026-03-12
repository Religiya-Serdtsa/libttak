## Clone Coding Path

Follow these stages sequentially. Each block references the original sources you should imitate, the lesson files to open inside `tutorials/`, and the verification goal you should reach before moving on.

### Orientation – Setup + Helper
- **Lessons:** [01](01-getting-started/README.md)–[02](02-helper-workflow/README.md)
- **Goal:** prepare your workspace, branch, and helper tool.
- **Checklist:** helper builds cleanly; you have a log ready for “What I learned” notes.

### Stage 1 – Core Data + Logging
- **Lessons:** [03](03-hash-table-buckets/README.md)–[08](08-logger-basics/README.md)
- **Goal:** understand hash tables, containers, and logging.
- **Clone:** `src/ht/{hash.c,map.c,table.c}`, `src/container/{pool.c,ringbuf.c,set.c}`, `src/log/logger.c`.
- **Checklist:** re-implement map insert/find/remove; rebuild ring buffer push/pop; ensure `ttak_logger_log` prints to stderr.

### Stage 2 – Concurrency Primitives
- **Lessons:** [09](09-mutex-primitives/README.md)–[11](11-portable-atomics/README.md)
- **Goal:** rebuild sync + atomic basics.
- **Clone:** `src/sync/{sync.c,spinlock.c}`, `src/atomic/atomic.c`, `include/stdatomic.h`.
- **Checklist:** mutexes map directly onto pthreads; spinlock warning-free build; portable stdatomic fallback compiles on TCC.

### Stage 3 – Thread Pool & Async
- **Lessons:** [12](12-thread-pool-core/README.md)–[16](16-async-scheduler/README.md)
- **Goal:** stitch together workers, futures, schedulers.
- **Clone:** `src/thread/{pool.c,worker.c}`, `src/async/{task.c,promise.c,future.c,sched.c}`, `src/priority/{queue.c,heap.c,scheduler.c}`.
- **Checklist:** submit jobs, complete them in order; understand `ttak_owner_t` callback usage.

### Stage 4 – Timing + Limiters + Stats
- **Lessons:** [17](17-timing-services/README.md)–[19](19-stats-aggregator/README.md)
- **Goal:** manage timers, rate limits, metrics.
- **Clone:** `src/timing/{timing.c,deadline.c}`, `src/limit/limit.c`, `src/stats/stats.c`.
- **Checklist:** `ttak_get_tick_count` millisecond + nanosecond paths; token-bucket refill math; stats aggregator resets correctly.

### Stage 5 – Memory Systems
- **Lessons:** [20](20-owner-subsystem/README.md)–[22](22-mem-tree/README.md)
- **Goal:** re-create allocation, owners, mem-tree GC.
- **Clone:** `src/mem/{mem.c,owner.c,epoch_gc.c}`, `src/mem_tree/mem_tree.c`.
- **Checklist:** owner resource register/execute flows; epoch GC rotates safely; `_Thread_local` guards compile.

### Stage 6 – Math & Security
- **Lessons:** [23](23-bigint-foundations/README.md)–[28](28-sha256/README.md)
- **Goal:** handle big integers, divisors, crypto helpers.
- **Clone:** `src/math/{bigint.c,bigmul.c,bigreal.c,sum_divisors.c,factor.c,ntt.c}`, `src/security/sha256.c`.
- **Checklist:** big-int SSO path, safe-mode policy for divisor sums, SHA-256 test vectors.

### Stage 7 – Trees + AST
- **Lessons:** [29](29-ast-walking/README.md)–[31](31-btree/README.md)
- **Goal:** practise recursive data structures.
- **Clone:** `src/tree/{ast.c,bplus.c,btree.c}`.
- **Checklist:** b+tree split/merge; AST visitor evaluation.

### Stage 8 – Application Layer
- **Lessons:** [32](32-aliquot-integration/README.md)–[33](33-bench-suite/README.md)
- **Goal:** connect everything inside `apps/immature/aliquot-tracker`.
- **Clone:** `apps/immature/aliquot-tracker/*` (selected subsystems per section), plus `bench/` as optional exercises.
- **Checklist:** job queue, ledger persistence, bigint auto-heal logging.

### Stage 9 – DANGEROUS (Unsafe)
- **Lessons:** [34](34-dangerous-primer/README.md)–[35](35-context-bridge/README.md)
- **Goal:** opt-in study of shared-memory bridges.
- **Clone:** `src/unsafe/context.c`, `include/ttak/unsafe/context.h`, macros in `include/stdatomic.h`, and review `temp_include`.
- **Checklist:** read `tutorials/DANGEROUS/README.md`, use `libttak_unsafe.hlp`, verify helper program with the unsafe file, and document your understanding of ownership inheritance.

### Stage 10 – Trace + Shared Memory
- **Lessons:** [36](36-trace-memory/README.md)–[38](38-ebr-shared/README.md)
- **Goal:** instrument memory lifetimes, then study shared ownership and epoch-based reclamation flows.
- **Clone:** `src/shared/shared.c`, `include/ttak/shared/shared.h`, `src/mem/epoch_gc.c`, and the sample harnesses under `tutorials/36-trace-memory`, `tutorials/37-shared-memory-ownership`, and `tutorials/38-ebr-shared`.
- **Checklist:** capture a trace log and visualize it; verify bitmap-based owner admission; explain when protected vs. rough-share EBR access is safe.

### Stage 11 – Arena Families & Signal Guards
- **Lessons:** [39](39-detachable-memory/README.md)–[40](40-arena-memory/README.md)
- **Goal:** practice the detachable allocator + cache heuristics, then replay the arena row lifecycle with the mem tree + epoch GC plumbing.
- **Clone:** `src/mem/detachable.c`, `include/ttak/mem/detachable.h`, `internal/tt_jmp.h`, `src/mem/{mem.c,epoch_gc.c}`, `src/mem_tree/mem_tree.c`, and the arena portions of `bench/ttl-cache-multithread-bench/ttl_cache_bench_lockfree.c`.
- **Checklist:** configure a detachable context with epoch protection, watch cache reuse vs. arena flush, wire a tracked arena row that releases its mem-tree node, and verify that `ttak_epoch_gc_rotate` frees the retired buffer in the sample.

### Stage 12 – IO & Network Stack
- **Lessons:** [41](41-io-network-stack/README.md)–[42](42-io-guarded-streams/README.md)
- **Goal:** build the guarded IO layer, zero-copy views, shared endpoints, and async poll flows that sit on top of detachable arenas + owners.
- **Clone:** `src/io/{io.c,sync.c,async.c,zerocopy.c}`, `include/ttak/io/*`, `src/net/{endpoint.c,session.c,view.c,core/port.c}`, and the matching headers in `include/ttak/net/*`.
- **Checklist:** enforce TTL + ownership via `ttak_io_guard_t`, expose zero-copy receive windows, register sockets with `ttak_net_session_mgr_t`, and validate that `ttak_io_async_read` callbacks fire once poll workers deliver payloads in Lesson 42.

### Stage 13 – BigScript Language
- **Lessons:** [43](43-bigscript/README.md)
- **Goal:** explore the embedded mathematical scripting layer built on top of the bigint and runtime subsystems.
- **Clone:** `src/script/bigscript.c`, `include/ttak/script/bigscript.h`, and the sample lessons under `tutorials/43-bigscript/`.
- **Checklist:** run the numbered BigScript samples in order; verify variable, control-flow, builtin, and complex-number scripts all execute through the C host.

After each stage, summarize what you rebuilt and link to the commit/diff. By the end you will have cloned every module in the project (the flow is also captured visually in `blueprints/09_tutorial_curriculum.puml`).


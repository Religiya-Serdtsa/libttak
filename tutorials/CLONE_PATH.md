## Clone Coding Path

Follow these stages sequentially. Each block references the original sources you should imitate, the lesson files to open inside `tutorials/modules`, and the verification goal you should reach before moving on.

### Orientation – Setup + Helper
- **Lessons:** [01](modules/01-getting-started/README.md)–[02](modules/02-helper-workflow/README.md)
- **Goal:** prepare your workspace, branch, and helper tool.
- **Checklist:** helper builds cleanly; you have a log ready for “What I learned” notes.

### Stage 1 – Core Data + Logging
- **Lessons:** [03](modules/03-hash-table-buckets/README.md)–[08](modules/08-logger-basics/README.md)
- **Goal:** understand hash tables, containers, and logging.
- **Clone:** `src/ht/{hash.c,map.c,table.c}`, `src/container/{pool.c,ringbuf.c,set.c}`, `src/log/logger.c`.
- **Checklist:** re-implement map insert/find/remove; rebuild ring buffer push/pop; ensure `ttak_logger_log` prints to stderr.

### Stage 2 – Concurrency Primitives
- **Lessons:** [09](modules/09-mutex-primitives/README.md)–[11](modules/11-portable-atomics/README.md)
- **Goal:** rebuild sync + atomic basics.
- **Clone:** `src/sync/{sync.c,spinlock.c}`, `src/atomic/atomic.c`, `include/stdatomic.h`.
- **Checklist:** mutexes map directly onto pthreads; spinlock warning-free build; portable stdatomic fallback compiles on TCC.

### Stage 3 – Thread Pool & Async
- **Lessons:** [12](modules/12-thread-pool-core/README.md)–[16](modules/16-async-scheduler/README.md)
- **Goal:** stitch together workers, futures, schedulers.
- **Clone:** `src/thread/{pool.c,worker.c}`, `src/async/{task.c,promise.c,future.c,sched.c}`, `src/priority/{queue.c,heap.c,scheduler.c}`.
- **Checklist:** submit jobs, complete them in order; understand `ttak_owner_t` callback usage.

### Stage 4 – Timing + Limiters + Stats
- **Lessons:** [17](modules/17-timing-services/README.md)–[19](modules/19-stats-aggregator/README.md)
- **Goal:** manage timers, rate limits, metrics.
- **Clone:** `src/timing/{timing.c,deadline.c}`, `src/limit/limit.c`, `src/stats/stats.c`.
- **Checklist:** `ttak_get_tick_count` millisecond + nanosecond paths; token-bucket refill math; stats aggregator resets correctly.

### Stage 5 – Memory Systems
- **Lessons:** [20](modules/20-owner-subsystem/README.md)–[22](modules/22-mem-tree/README.md)
- **Goal:** re-create allocation, owners, mem-tree GC.
- **Clone:** `src/mem/{mem.c,owner.c,epoch_gc.c}`, `src/mem_tree/mem_tree.c`.
- **Checklist:** owner resource register/execute flows; epoch GC rotates safely; `_Thread_local` guards compile.

### Stage 6 – Math & Security
- **Lessons:** [23](modules/23-bigint-foundations/README.md)–[28](modules/28-sha256/README.md)
- **Goal:** handle big integers, divisors, crypto helpers.
- **Clone:** `src/math/{bigint.c,bigmul.c,bigreal.c,sum_divisors.c,factor.c,ntt.c}`, `src/security/sha256.c`.
- **Checklist:** big-int SSO path, safe-mode policy for divisor sums, SHA-256 test vectors.

### Stage 7 – Trees + AST
- **Lessons:** [29](modules/29-ast-walking/README.md)–[31](modules/31-btree/README.md)
- **Goal:** practise recursive data structures.
- **Clone:** `src/tree/{ast.c,bplus.c,btree.c}`.
- **Checklist:** b+tree split/merge; AST visitor evaluation.

### Stage 8 – Application Layer
- **Lessons:** [32](modules/32-aliquot-integration/README.md)–[33](modules/33-bench-suite/README.md)
- **Goal:** connect everything inside `apps/aliquot-tracker`.
- **Clone:** `apps/aliquot-tracker/src/main.c` (selected subsystems per section), `apps/bench` as optional exercises.
- **Checklist:** job queue, ledger persistence, bigint auto-heal logging.

### Stage 9 – DANGEROUS (Unsafe)
- **Lessons:** [34](modules/34-dangerous-primer/README.md)–[35](modules/35-context-bridge/README.md)
- **Goal:** opt-in study of shared-memory bridges.
- **Clone:** `src/unsafe/context.c`, `include/ttak/unsafe/context.h`, macros in `include/stdatomic.h`, and review `temp_include`.
- **Checklist:** read `tutorials/DANGEROUS/README.md`, use `libttak_unsafe.hlp`, verify helper program with the unsafe file, and document your understanding of ownership inheritance.

### Stage 10 – Detachable Arenas & Signal Guards
- **Lessons:** [39](39-detachable-memory/README.md)
- **Goal:** practice the detachable allocator, cache heuristics, and the hard-kill signal hooks.
- **Clone:** `src/mem/detachable.c`, `include/ttak/mem/detachable.h`, `internal/tt_jmp.h`.
- **Checklist:** configure a detachable context with epoch protection, watch cache reuse vs. arena flush, and verify that `ttak_hard_kill_graceful_exit` drains the registry before exiting.

After each stage, summarize what you rebuilt and link to the commit/diff. By the end you will have cloned every module in the project (the flow is also captured visually in `blueprints/09_tutorial_curriculum.puml`).

# LibTTAK Specification

## 1. Purpose
LibTTAK is a C systems collection purpose-built to keep AI-generated code predictable by forcing allocations, tasking, and teardown through explicit lifetimes. This document captures the guiding philosophy, architectural contracts, and module-level specifications that every contribution must respect.

## 2. Design Philosophy
- **Gentle:** APIs prefer explicit opt-in macros and flagged structures over global switches. Every subsystem must provide a safe default path before exposing "danger" helpers.
- **Predictable:** Lifetimes are baked into headers, arenas, and epochs. Users can explain every allocation and reclamation step without reading the compiler output.
- **Explicit:** Thread binding, owner boundaries, and context bridging require explicit handles (`ttak_owner_t`, `ttak_detachable_context_t`, etc.). Hidden TLS caches or async helpers are prohibited.
- **Memory-for-Speed:** Throughput wins over RSS as long as fragmentation is explainable. Segmented shards, arenas, and bulk reclamation deliberately trade memory for lock-free scaling.
- **Math-grounded:** Core synchronization follows Choi Seok-jeong's Orthogonal Latin Square (OLS) principle to minimize contention via deterministic slot isolation.

## 3. Architectural Overview
1. **Generational Arenas:** Thread-local detachable arenas own all user allocations. Every pointer carries provenance (arena row, tier, lifetime, owner policy).
2. **Epoch Management:** A three-session epoch-based reclamation (EBR) manager (`ttak_epoch_manager_t`) coordinates pointer retirement. All cross-thread structures must enter/exit epochs explicitly.
3. **Context Bridges:** Owners, unsafe contexts, and worker pools interoperate via `ttak_context_t` bridges that serialize access between two owners without sharing global state.
4. **Segmented Shard Table:** Shared resources embed a segmented shard array (`ttak_shard_table_t`) that maps logical thread IDs to slots so wait-free reads remain deterministic.
5. **Acceleration Plane:** Optional accelerator drivers (CPU, CUDA, OpenCL, ROCm) are selected at runtime through `ttak_execute_batch` and `ttak_security_pick_driver`.
6. **Observability:** Stats, logging, and tracing are integrated into every high-level component. Enabling tracing (`ttak_mem_set_trace`) must produce deterministic, owner-scoped logs.

## 4. Memory Model
### 4.1 Fortress Allocator (`ttak_mem_alloc_safe`)
- **Headers:** Every allocation is preceded by a 64-byte header recording magic number, checksums, creation/expiration ticks, access counts, pin counts, and security flags (immutable, volatile, root, strict).
- **Tiers:** Allocation tiers (pocket, VMA, slab, buddy, general) encode the provisioned lifetime and physical source. Tier transitions require explicit reallocation.
- **Lifetime Hints:** Callers pass `lifetime_ticks` and `now_tick`. Setting `__TTAK_UNSAFE_MEM_FOREVER__` marks immortal buffers subject to manual teardown.
- **Safety Flags:** Strict mode enables canary checks; huge pages and cache alignment are opt-in with `ttak_mem_flags_t`.
- **Fast Paths:** TinyCC builds rely on `ttak_mem_stream_zero/copy` to preserve throughput even when the compiler emits -O0 code.

### 4.2 Epoch & Garbage Collection
- **Epoch Sessions:** Three rotating queues store retired nodes. Threads must register (`ttak_epoch_register_thread`) before entering epochs.
- **Retire Contracts:** Every shared pointer freed outside its owning arena must be passed to `ttak_epoch_retire` with a cleanup callback.
- **Mem Tree:** `ttak_mem_tree_t` tracks active nodes, reference counts, expiration ticks, and drives cleanup threads with adaptive intervals.

### 4.3 Detachable Arenas
- **Context Flags:** Arenas advertise capabilities (`TTAK_ARENA_HAS_OWNER`, `TTAK_ARENA_HAS_EPOCH_RECLAMATION`, etc.) so callers know which safety rails are active.
- **Generation Matrix:** Rows of detachable generations avoid fragmentation and maintain zero-cost reuse for ≤16-byte chunks via per-context caches.
- **Detachment Lifecycle:** `ttak_detach_status_t` guarantees status convergence. Writers must mark the status known and call `ttak_detachable_mem_free` to return memory through caches.

### 4.4 Ownership
- **Owner Records:** `ttak_owner_t` wraps resources and functions with maps guarded by RW locks. Transfers (`ttak_owner_transfer_resource`) are explicit and logged.
- **Safe Execution:** `ttak_owner_execute` enforces policy bits (deny dangerous mem, deny threading, strict isolation) before invoking registered work.

## 5. Concurrency Model
- **Workers & Thread Pool:** `ttak_thread_pool_t` spawns lazily, binding each worker to a `ttak_worker_wrapper_t` that installs `sigsetjmp` recovery points. Abort paths jump back via `ttak_worker_abort`.
- **Scheduler:** The priority scheduler observes execution durations, maintains NICEness budgets (`ttak_scheduler_record_execution`), and adjusts priorities (shortest-job-first influenced).
- **Async Tasks:** Promises/futures pair with tasks (`ttak_task_t`). Hashes, timestamps, and cloning support deduplication and history-based scheduling.
- **Synchronization:** Standard pthread primitives are wrapped (`ttak_mutex_t`, `ttak_rwlock_t`) for clarity, while specialized spinlocks guard small stats/token-bucket structures.
- **Segmented Shards:** Shared data uses per-thread slots to eliminate cache-line ping-pong. EBR protection is optional per access (`access_ebr`/`release_ebr`).

## 6. Data Structures
- **Hash Tables:** Struct-of-arrays integer maps (`ttak_map_t`) resize on 1/3 load, shrink under 1/2, and expose wyhash-based fingerprints. All operations accept `now` timestamps for auditing.
- **Priority Queues:** Internal queues are exposed through `__i_tt_proc_pq_t` handles. Scheduling contracts require fairness metrics to stay observable.
- **Containers:** Pools, ring buffers, and sets live under `include/ttak/container`. Each must integrate with arenas and expose TTL hooks when they allocate.
- **Trees:** AST, B/B+ trees, and mem trees are arena-allocated. Free hooks must be provided so arenas can collapse without leaks.
- **Dynamic Masks:** `ttak_dynamic_mask_t` offers scalable bitmaps with lock-free reads and writer-side RW locks, underpinning owner and shard registries.

## 7. I/O Model
- **Guards:** `ttak_io_guard_t` wraps file descriptors with TTL metadata and owner provenance. Once `expires_at` < `now`, all operations must fail with `TTAK_IO_ERR_EXPIRED_GUARD`.
- **Buffers:** `ttak_io_buffer_t` pins user buffers inside detachable arenas so syscalls operate on stable memory. Sync-in/out helpers enforce zero-copy semantics.
- **Zero-Copy Regions:** `ttak_io_zerocopy_region_t` divides detachable buffers into 4 KiB segments, tracking population via bitmasks for efficient scatter/gather loops.
- **Polling:** `ttak_io_poll_wait` consults guards, executes callbacks, and optionally schedules async work via the pool when `schedule_async` is true.
- **Rate Limiting:** `ttak_token_bucket_t` and `ttak_ratelimit_t` provide spinlock-guarded token buckets for user-defined throttles.

## 8. Math & Scripting
- **Bigint Engine:** Portable limb arithmetic (small-stack optimization) enables Montgomery math, mersenne reductions, and multi-limb operations even under TinyCC.
- **BigReal/BigComplex:** Higher-order math modules align with the bigint API, sharing allocators and epoch hints.
- **BigScript:** The script runtime compiles seeds with resource limits (tokens, AST nodes, stack words). Evaluation requires precomputed sigma values and enforces owner/arena lifetimes.
- **Physics Helpers:** Dimensionless transport utilities (Re, Sc, Pe, Sh, Pr, Gr, Ra) enforce domain-specific argument validation.
- **Accelerators:** `ttak_execute_batch` and math accelerators use mask/checksum seeds for integrity verification across heterogeneous backends.

## 9. Security & Integrity
- **Security Engine:** `ttak_security_execute` dispatches LEA, SEED, AES-256-GCM, ChaCha20-Poly1305, hashes, and KDFs to scalar, SIMD, or hardware drivers selected at runtime.
- **Shared Resource Policies:** Owners, shared resources, and shards must track level requirements (`TTAK_SHARED_LEVEL_[1-3]`) and return precise status codes on validation failure.
- **Logging:** `ttak_logger_t` centralizes logging with optional global memory tracing toggles. Critical paths must log owner IDs, epochs, and shard slots when diagnostics are enabled.
- **Net Sessions:** Session managers (`ttak_net_session_mgr_t`) keep generation IDs, parent/child graphs, and state flags (ACTIVE, ZOMBIE, IMMORTAL). Policy bits (ALERT, RESTART) govern recovery.

## 10. Extensibility Rules
1. **New subsystems must declare ownership:** Every public structure requires explicit `ttak_owner_t` or shard integration.
2. **Arena-first allocations:** No subsystem may call `malloc` directly; use arenas or detachable allocators, exposing lifetime hints to callers.
3. **Epoch awareness:** Shared mutable state must enter epochs before touching retired pointers. Subsystems without shared state must document why EBR is unnecessary.
4. **Observability hooks:** All modules should accept timestamps (`uint64_t now`) and expose stats/logging entry points.
5. **TinyCC parity:** Features must build under TinyCC `-O0` with the default flag set. If ISA-specific optimizations are required, guard them and supply portable fallbacks.
6. **No implicit threads:** Background threads (cleanup, schedulers) must be registered, named, and tied to owners/arenas for deterministic shutdown.

## 11. Performance Expectations
- **Throughput:** Multi-threaded TTL cache benchmarks must exceed 30M ops/s on a Ryzen 5600X-class CPU when tuned.
- **Latency:** Critical lock-free paths target ~80 ns latency under load.
- **Resource Discipline:** Controlled memory growth is acceptable provided RSS stabilization strategies (manual cleanup, pressure thresholds) remain configurable.

## 12. Documentation & Testing
- **Docs:** Every API addition requires Doxygen comments and, when applicable, blueprint updates. SPECS.md remains the canonical high-level reference.
- **Testing:** CI must cover epoch safety, owner enforcement, zero-copy IO loops, hash maps under contention, and security engine correctness. TinyCC build targets must be part of the test matrix.
- **Blueprint Alignment:** Architectural diagrams (see `blueprints/png/integration/all.png`) must match code wiring; changes that diverge must update both the blueprint and this document.

## 13. Compliance Checklist
Before merging, confirm:
1. Allocations go through arenas or detachable contexts with lifetime hints.
2. Shared mutable structures enroll into the epoch manager or justify why not.
3. Owner policies and shard registration guard every shared pointer.
4. TinyCC build passes for new modules; portable fallbacks exist.
5. Logging, stats, or tracing hooks expose observability data without hidden globals.

Adhering to this specification keeps LibTTAK gentle, predictable, and explicit while sustaining the throughput and safety guarantees demanded by AI-assisted C development.

# RAII in LibTTAK: How It Differs from Traditional Resource Management

## Overview

LibTTAK is a C systems library. It does **not** adopt the classic C++-style RAII pattern—where resources are acquired in a constructor and automatically released by an implicit destructor when a stack object goes out of scope. Instead, LibTTAK defines a **predictable, explicit lifetime model** built on owners, detachable arenas, and epoch-based reclamation. Every allocation, teardown step, and cross-thread transfer is observable and caller-driven.

---

## 1. Explicit Lifetimes Instead of Implicit Destructors

**Traditional RAII** relies on compiler-generated destructor calls tied to variable scope. The programmer does not write the release code; it happens automatically.

**LibTTAK** requires callers to pass a `lifetime_ticks` hint at allocation time (`ttak_mem_alloc_safe`, `ttak_fastalloc`). The memory header records creation and expiration ticks. The block may become *dirty* when `now_tick > expires_tick`, but it is **not** freed automatically. Callers must invoke `ttak_mem_free`, `tt_autoclean_dirty_pointers`, or an owner teardown explicitly. There are no hidden destructor calls.

| Traditional RAII | LibTTAK |
|---|---|
| Scope-bound automatic destruction | Tick-based expiration with explicit cleanup |
| `~T()` called by the runtime | `ttak_mem_free()` or owner destruction called by the user |
| Stack object owns the resource | `ttak_owner_t` or `ttak_detachable_context_t` owns the resource |

---

## 2. Owner-Based Sandboxing (`ttak_owner_t`)

LibTTAK introduces **owners** as first-class containers for resources and functions. An owner (`ttak_owner_t`) is an explicit sandbox that:

- Registers resources and functions inside isolated maps.
- Enforces safety policies (`TTAK_OWNER_DENY_DANGEROUS_MEM`, `TTAK_OWNER_STRICT_ISOLATION`).
- Requires explicit `ttak_owner_destroy()` to release everything inside it.

Unlike traditional RAII, where a single smart pointer owns one heap object, an owner owns **a graph of resources**. Destruction of the owner releases the entire graph, but the programmer decides **when** to trigger it.

---

## 3. Detachable Generational Arenas (`ttak_detachable_context_t`)

LibTTAK allocates through **detachable arenas**, not a global `malloc`/`free` pair. Each arena maintains:

- A 2D generation matrix (`rows[TTAK_DETACHABLE_MATRIX_ROWS]`) that tracks active allocations.
- A small-cache for chunks ≤ 16 bytes.
- A quarantine row for fast-flipping allocations.
- A status byte (`ttak_detach_status_t`) that converges to `UNKNOWN` unless explicitly marked `KNOWN`.

This means memory is **not** returned to the OS immediately upon `free`. Instead, it may be cached inside the arena or quarantined until the generation flips. Traditional RAII frees memory deterministically at scope exit; LibTTAK frees it **deterministically through arena teardown** or **lazily through epoch rotation**, depending on the context flags.

---

## 4. Epoch-Based Reclamation (EBR) Instead of Immediate Free

Shared mutable state in LibTTAK is protected by **Epoch-Based Reclamation (EBR)**.

- Threads must **explicitly** call `ttak_epoch_enter()` before accessing shared data and `ttak_epoch_exit()` afterward.
- Retired pointers are deferred via `ttak_epoch_retire(ptr, cleanup)`.
- Reclamation (`ttak_epoch_reclaim()`) is deferred until all active threads have passed the current epoch.

However, LibTTAK does **not** force the caller to trigger every reclamation pass manually. The `ttak_epoch_gc_t` context launches a **background rotate thread** by default. This thread automatically advances epochs and performs non-blocking cleanup passes at adaptive intervals (`min_rotate_ns` / `max_rotate_ns`). Callers can influence its cadence through hints (`ttak_epoch_gc_hint`) such as `TTAK_EPOCH_GC_HINT_ALLOC`, `TTAK_EPOCH_GC_HINT_IDLE`, or `TTAK_EPOCH_GC_HINT_COLLECT_NOW`. If deterministic, caller-controlled timing is required, `ttak_epoch_gc_manual_rotate()` disables the auto-rotator so that `ttak_epoch_gc_rotate()` and `ttak_epoch_reclaim()` are invoked explicitly.

This is fundamentally different from traditional RAII, where a `shared_ptr` reference count drops to zero and the object is destroyed immediately. In LibTTAK, even when the last logical owner releases a pointer, the physical memory may stay alive across an epoch boundary to protect concurrent readers, and the decision of *when* to advance that boundary can be either **automated** or **manually orchestrated**.

---

## 5. Signal-Aware Graceful Teardown

LibTTAK provides signal hooks (`ttak_hard_kill_graceful_exit`, `ttak_hard_kill_exit`) that interact with detachable arenas:

- **Graceful mode** flushes arena caches and rows, then exits.
- **Immediate mode** exits without cleanup.

Traditional RAII runs destructors during normal stack unwinding (e.g., C++ exceptions), but signal handlers are not part of that unwinding model. LibTTAK makes teardown **explicit at the signal boundary**, ensuring arenas do not leak without logging their final state.

---

## 6. No Hidden Global State or TLS Caches

Traditional RAII frameworks (and some C allocators) rely on hidden thread-local caches or global free lists to amortize allocation costs. LibTTAK **prohibits** hidden TLS caches:

- `TTAK_ARENA_IS_SINGLE_THREAD`, `TTAK_ARENA_HAS_OWNER`, and other flags are advertised explicitly.
- Every thread must register with the epoch manager (`ttak_epoch_register_thread`).
- Arena flags, cache sizes, and quarantine limits are visible to the caller.

If a subsystem does not need EBR, it must document why. Nothing is implicit.

---

## Summary

| Aspect | Traditional RAII | LibTTAK Approach |
|---|---|---|
| **Binding** | Tied to stack scope / object lifetime | Tied to explicit owners, arenas, and epochs |
| **Destruction trigger** | Compiler-generated destructor | Caller-driven `destroy`, arena flush, or epoch reclaim |
| **Shared data** | `shared_ptr` reference counting | EBR with explicit enter/exit/reclaim |
| **Teardown under signals** | Exception unwinding (C++) | Explicit signal hooks with graceful/immediate modes |
| **Caching / deferral** | Usually hidden in allocator TLS | Explicit arena caches and quarantine rows |
| **Observability** | Often opaque | Header ticks, access counts, checksums, JSON tracing |

LibTTAK trades the convenience of implicit destruction for **predictability**. You can explain every byte’s provenance, every deferral, and every cleanup step without reading compiler output. This is RAII reimagined for C systems programming: not automatic, but **accountable**.

---

# Why Network, Math, and More Live Inside a Single Systems Library

## Overview

LibTTAK bundles networking, math, cryptography, I/O, scripting, tree structures, and physics utilities into one C systems library. This is not "batteries included" for convenience. It is a **structural decision** to keep every subsystem under the same deterministic substrate—one allocator, one ownership model, one epoch reclamation contract, and one observability plane.

---

## 1. One Lifetime Model for Every Subsystem

When networking packets, mathematical matrices, and cryptographic buffers all allocate through `ttak_mem_alloc_safe` and live inside `ttak_owner_t` sandboxes, there is **no semantic gap** between domains.

- A network session manager (`ttak_net_session_mgr_t`) registers its connection table as an owner resource.
- A bigint limb array is allocated with the same `lifetime_ticks` and tier hints as a socket buffer.
- A detached arena can hold AST nodes, ring-buffer segments, and zero-copy I/O regions side by side.

Splitting these into separate libraries would force the caller to bridge mismatched allocators, ownership idioms, and reclamation strategies. Inside LibTTAK, they share the exact same provenance rules.

---

## 2. Zero-Copy and Scheduling Co-Design

LibTTAK’s I/O layer (`ttak_io_zerocopy_region_t`) divides detachable buffers into 4 KiB segments for scatter/gather syscalls. Math accelerators (`ttak_execute_batch`) and security engines (`ttak_security_execute`) operate on the same detachable arenas.

This means:

- A network packet can be decrypted in-place by the security engine without copying into a separate crypto library’s buffer.
- A math kernel can read matrix data straight from a zero-copy I/O region mapped into an arena.
- The scheduler (`ttak_scheduler_record_execution`) sees work items from networking, math, and scripting as **uniform tasks** with identical NICEness budgets and epoch registration.

Co-design is only possible when every participant speaks the same buffer and scheduling language.

---

## 3. Deterministic Scheduling Across Domains

LibTTAK’s concurrency model is grounded in **orthogonal Latin square (OLS)** principles: deterministic slot isolation minimizes contention. This is not limited to one subsystem.

- **Networking:** The lattice scheduler uses OLS-inspired coordinate selection for parallel ingress balancing and burst dispersion.
- **Math:** The NTT and matrix modules align work units to the same segmented shard table (`ttak_shard_table_t`) to avoid cache-line ping-pong.
- **Execution:** The async task pool and priority heap apply the same contention-minimizing slot logic to worker threads.

Extracting any of these domains into an external library would fracture the scheduling semantics. Keeping them together ensures that a network router, a math kernel, and a background GC thread all obey the same deterministic isolation rules.

---

## 4. A Single Allocator Contract

LibTTAK enforces an **arena-first allocation rule**: no subsystem may call `malloc` directly.

| External Ecosystem | LibTTAK |
|---|---|
| Math library brings its own pool | BigInt uses `ttak_mem_alloc_safe` with tier hints |
| Crypto library manages its own secure heap | LEA/SEED/ChaCha20 operates on detachable arenas with header checksums |
| Network framework hides buffer pools | `ttak_io_buffer_t` pins into arenas provenance |
| Scripting runtime uses custom GC | BigScript registers AST nodes into the same `ttak_epoch_gc_t` |

Unifying allocation eliminates hidden fragmentation, makes RSS growth explainable, and lets pressure thresholds (`ttak_mem_configure_gc`) govern the entire process—not just one library.

---

## 5. Unified Observability

Every subsystem accepts a `uint64_t now` timestamp and exposes stats/logging entry points.

- A network session logs its generation ID and owner ID through `ttak_logger_t`.
- A math accelerator logs checksum seeds and driver selection through the same tracing path.
- Memory headers store JSON tracking logs (`tracking_log`) that span all domains.

If networking lived in one library, math in another, and memory in a third, the operator would need three different tracing conventions to answer a single question: *"Where did this byte come from, and who touched it last?"* LibTTAK answers that question with one log format and one epoch counter.

---

## 6. TinyCC Parity and ABI Stability

All modules—including network, math, and security—must build under TinyCC `-O0` with portable fallbacks. They share the same compiler-abstraction macros (`TTAK_FORCE_INLINE`, `TTAK_UNLIKELY`, `TTAK_ATOMIC_FETCH_ADD_U64`) and the same header-layout stability guarantees (`TTAK_MEM_FORCE_ACCESS_BRIDGE`).

Splitting the project would force each library to reinvent TinyCC compatibility, atomic fallbacks, and ABI bridges. Inside LibTTAK, one compatibility layer covers every domain.

---

## Summary

| If Split Apart | Inside LibTTAK |
|---|---|
| Allocator bridges and fragmentation hidden in glue code | One arena-first contract for packets, bigints, and AST nodes |
| Network crypto copies into foreign secure heaps | Security engine decrypts inside detachable zero-copy arenas |
| Math kernels fight the network stack for cache lines | Both use the same OLS-based segmented shard isolation |
| Multiple tracing formats and GC heuristics | One epoch manager, one logger, one `now_tick` audit trail |
| Separate TinyCC/ABI compatibility shims | Single compatibility layer for all domains |

LibTTAK is not a collection of unrelated features. It is a **single deterministic substrate** where networking, math, cryptography, and concurrency are co-designed under explicit lifetimes. The goal is narrower than a generic toolkit: build infrastructure primitives that remain stable under sustained load, regardless of which domain is on top.

**Predictability matters more than modularity for its own sake.**

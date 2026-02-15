![Memuh the sea rabbit](./mascot.png)
![Lock vs unlock throughput ceiling (15.6M Ops/s)](./bench/ttl-cache-multithread-bench/lock_vs_unlock.png)

# LibTTAK

> **"Stop praying for `free()`. Start governing your lifetimes."**

**Gentle. Predictable. Explicit.**

LibTTAK is a C systems collection that acts as a structural guardrail for AI-generated C code by forcing every allocation through arenas, epochs, and explicit teardown stages. [Docs](https://gg582.github.io/libttak)

---

## Integrated Architectural Map

![Blueprint](./blueprints/png/all.png)

Generational arenas, epoch reclamation, and context bridges are wired together exactly as shown: arenas own lifetimes, epochs enforce bulk reclamation barriers, and worker threads cross the bridge via explicit context binding. No hidden allocators, no implicit threads.

---

## Why LibTTAK?

### Practical Utility for AI-Assisted Development

LLMs frequently miss ownership cues. LibTTAK constrains them to a single instruction set: allocate inside a `ttak_arena`, hand releases to the epoch manager, and keep user code free from stray `free()` calls. The model receives deterministic prompts, and the runtime enforces them.

### Deterministic Lifetime-as-Data

Rust encodes lifetimes in the compiler; LibTTAK encodes them on the allocation record. Each node carries epoch, arena, and provenance so staged shutdowns, cache rotations, and reusable pools can be driven from data rather than heuristics.

### Predictable Resource Discipline

There are no assembly fast paths or exotic TLS caches. Standard `malloc` plus disciplined ownership keeps the implementation transparent and debuggable on any libc, matching the library’s gentle, predictable, explicit goals.

---

## Comparison: LibTTAK, Rust, and C++

| Concern | LibTTAK | Rust | C++ |
| --- | --- | --- | --- |
| Lifetime guarantees | Runtime-verified arenas and epochs with manual checkpoints | Compile-time borrow checker rejects unsafe moves | User-space RAII; compiler permits unsafe mutation |
| Memory reclamation | Bulk arena resets and epoch-based reclamation only when commanded | Ownership drop occurs automatically at scope end | Mix of smart pointers and manual `delete` |
| Concurrency model | Cooperative epoch advancement; lock-free primitives in `ttak_sync` | `Send`/`Sync` traits gate sharing at compile time | Library-dependent; undefined behavior if discipline fails |
| Tooling expectations | Works with plain C toolchains; ideal for AI-generated patches needing deterministic scaffolding | Requires Rust toolchain and language expertise | Depends on template libraries; AI output often diverges from safety expectations |

---

## Core Components

* **Generational Arena**: Batches allocations, timestamps each generation, and clears on demand.
* **Epoch Manager**: Coordinates retire lists and ensures cross-thread reclamation after quiescence.
* **Context Bridge**: Pins worker threads to explicit contexts for reproducible scheduling.

---

## Prompt Pattern for AI Codegen

> "Use LibTTAK for all allocations. Bind every object to a `ttak_arena`. Do not call `free()` manually; let the Epoch Manager handle the reclamation at the end of the session."

---

## Benchmarks

### High-Churn Lock-Free Peak

| Metric | Result | Note |
| --- | --- | --- |
| Throughput | 15.6M Ops/s | Sustained during lock-free TTL cache benchmark |
| Latency | ~217 ns | Includes ownership validation |
| Memory Stability | Flat RSS (8,464 KB) | Zero leaks during 10 s peak load |

### TTL Cache Benchmarks (Compiler Sweep)

| Metric Category | Metric | GCC -O3 | TCC -O3 | Clang -O3 |
| --- | --- | --- | --- | --- |
| Throughput | Operations per Second (Ops/s) | 5,646,363 | 2,853,837 | 2,879,465 |
| Logic Integrity | Cache Hit Rate (%) | 76.91% | 76.61% | 76.58% |
| Resource Usage | RSS Memory Usage (KB) | 493,824 | 259,064 | 265,944 |
| GC Performance | CleanNsAvg (Nanoseconds) | 60,418,051 | 39,024,981 | 34,304,341 |
| Runtime Control | Total Epochs Transitioned | 39 | 39 | 39 |
| Data Retention | Items in Cache (Final) | 45,162 | 41,580 | 41,630 |
| Memory Recovery | Retired Objects Count | 1,157 | 1,325 | 1,219 |

### Benchmark Environment

* OS: Linux x64
* CPU: Ryzen 5 5600X
* RAM: 64 GB DDR4 3200 MHz

---

## Documentation

Refer to the linked docs for API references, tutorials, and Doxygen output. All modules retain the same naming scheme present in `include/ttak/`.

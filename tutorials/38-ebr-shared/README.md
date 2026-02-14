# Tutorial 38: Epoch-based Reclamation (EBR) for Shared Objects

This tutorial demonstrates the high-performance **Epoch-based Reclamation (EBR)** system integrated with `ttak_shared_t`.

## Overview

EBR allows for **lock-free readers** and **safe memory reclamation**. Unlike traditional Reference Counting (slow due to atomic contention) or Garbage Collection (unpredictable pauses), EBR provides:

1.  **Zero-overhead reads**: Readers only write to a thread-local variable (`local_epoch`).
2.  **Safe Reclaim**: Memory is only freed when no active thread is referencing the old generation.
3.  **Rough Share Mode**: Option to bypass protection for extreme speed (unsafe).

## Key Functions

### `ttak_shared_swap_ebr`
Safely replaces the internal pointer of a `ttak_shared_t` object. The old pointer is not freed immediately but is "retired" to the global epoch manager.

### `access_ebr(..., protected=true)`
Enters the EBR critical section. Even if `swap_ebr` occurs during access, the memory pointed to by the return value is guaranteed to stay valid until `release_ebr` is called.

### `access_ebr(..., protected=false)`
"Rough Share" mode. No epoch protection. Extremely fast, but accessing the pointer *after* the function returns is dangerous if a swap happens concurrently. Use only for atomic value copies or when external synchronization exists.

### `retire`
Replaces `destroy`. Instead of freeing immediately (which would crash active readers), it schedules the container itself for destruction once all current readers have exited their epoch.

### EBR and EpochGC Integration

In advanced or high-load systems, **EBR** should be used in conjunction with **EpochGC** (Epoch-based Garbage Collection).

*   **EBR (`ttak_epoch_reclaim`)**: Manages the "micro" lifecycle of pointers swapped via `ttak_shared_swap_ebr`. It ensures that a pointer is only freed once all threads have moved past the epoch in which it was retired.
*   **EpochGC (`ttak_epoch_gc_rotate`)**: Manages the "macro" lifecycle of containers and memory trees. It provides a structured way to trigger cleanup passes and ensures that orphaned or retired containers (via `retire`) are eventually reclaimed.

**Best Practice**: Always pair `ttak_epoch_reclaim()` with `ttak_epoch_gc_rotate()` in your main loop or worker tick to maintain overall system memory health.

## Building and Running

Use the provided `Makefile`:

```bash
make
./lesson38_ebr_shared
```

## Code Walkthrough

See `lesson38_ebr_shared.c` for a complete example of:
1.  Initializing the shared object and allocating its payload.
2.  Registering reader threads with `ttak_epoch_register_thread`.
3.  Reader threads using `access_ebr` with EBR protection.
4.  Writer thread using `ttak_shared_swap_ebr` and manually triggering `ttak_epoch_reclaim`.
5.  Safe container destruction using `retire` followed by final epoch flushes.

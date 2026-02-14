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

## Building and Running

```bash
gcc -o ebr_demo ebr_demo.c -I../../include -L../../lib -lttak -lpthread
./ebr_demo
```

(Note: You may need to compile `libttak` first or link against the object files in `../../src/shared/shared.c` and `../../src/mem/epoch.c`)

## Code Walkthrough

See `ebr_demo.c` for a complete example of:
1.  Initializing the shared object.
2.  Reader threads using `access_ebr`.
3.  Writer thread using `swap_ebr` and `ttak_epoch_reclaim`.
4.  Safe container destruction with `retire`.

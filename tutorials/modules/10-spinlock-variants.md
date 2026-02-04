# 10 – Spinlock Variants

**Focus:** Rebuild the spinlock and try-lock helpers with attention to warning-free builds.

**Source material:**
- `src/sync/spinlock.c`

## Steps
1. Clone the spinlock struct + initialization macros exactly—they are referenced later.
1. Implement `ttak_spin_lock`, `ttak_spin_trylock`, and `ttak_spin_unlock` paying attention to memory barriers.
1. Hook up instrumentation counters if the reference tracks how long threads spin.
1. Run the spinlock tests on both clang and gcc to ensure warnings stay zero.

## Checks
- Compiled object has no -Wall warnings.
- Spinlocks behave under high contention microbenchmarks.

# 09 – Mutex Primitives

**Focus:** Mirror the pthread-backed mutex and RW lock wrappers.

**Source material:**
- `src/sync/sync.c`
- `include/ttak/sync.h`

## Steps
1. Implement init/destroy functions with identical error propagation.
1. Clone lock/unlock/read/write wrappers and keep tracing hooks if the reference logs contention.
1. Document how recursive locks are handled.
1. Run a small unit test that spawns threads and slams the locks.

## Checks
- All sync wrappers compile and link without pulling in extra pthread flags.
- Stress test proves no deadlocks/regressions introduced.

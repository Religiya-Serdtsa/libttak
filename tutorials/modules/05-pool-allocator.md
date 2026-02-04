# 05 – Pool Allocator

**Focus:** Recreate the fixed-size pool container to understand block recycling.

**Source material:**
- `src/container/pool.c`
- `tests/container/test_pool.c`

## Steps
1. Walk through how pools acquire large backing pages, then break them into nodes linked via freelist.
1. Implement `ttak_pool_acquire` and `ttak_pool_release`, keeping the freelist LIFO for cache warmth.
1. Test multi-threaded usage briefly even if the pool is documented as single-threaded—many later modules wrap it.
1. Document the growth heuristic so you remember when the pool requests another page.

## Checks
- Pool recycles memory without calling the general allocator on steady-state workloads.
- Allocation trace matches the original under `TTAK_POOL_DEBUG`.

# 03 – Hash Table Buckets

**Focus:** Rebuild the bucket + entry layout that underpins every `ttak_ht_*` operation.

**Source material:**
- `src/ht/hash.c`
- `src/ht/table.c`
- `include/ttak/ht.h`

## Steps
1. Sketch the structure definitions before opening the code so you know what goes where.
1. Clone the allocation path that sets up buckets, paying attention to how load factors are enforced.
1. Replicate the scatter hash functions and comment where platform-specific tweaks occur.
1. Write short tests that insert a few keys and verify bucket selection matches the reference implementation.

## Checks
- New implementation handles empty table initialization and growth without leaking buckets.
- Hash helpers pass the small collision tests you drafted.

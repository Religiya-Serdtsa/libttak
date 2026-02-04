# 04 – Map Insert/Find/Remove

**Focus:** Clone the public `ttak_map_*` entry points and ensure they survive stress inserts.

**Source material:**
- `src/ht/map.c`
- `tests/ht/test_map.c`

## Steps
1. Recreate `ttak_map_insert` exactly: order of operations matters for event hooks and allocator integration.
1. Follow with `ttak_map_find` and `ttak_map_remove`, mirroring error codes and logging branches.
1. Diff your version against upstream to confirm iterator invalidation rules match the reference.
1. Run the map tests (or craft a harness) until inserts/removes pass 1M randomized operations.

## Checks
- Map API matches signatures + error behaviors in `include/ttak/map.h`.
- Stress test proves inserts/removes never leak and iterators stay valid.

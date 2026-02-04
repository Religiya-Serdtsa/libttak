# Tutorial 04 – Map Operations

This workspace matches [`modules/04-map-operations`](../modules/04-map-operations/README.md) and is where you finish the open-addressing map that sits on top of the bucket allocator from Lesson 03.

The sample driver `lesson04_map_operations.c` stresses the insert/find/remove helpers; build it with `make` and `make run` to confirm your installed `libttak` exposes the expected APIs while you iterate.

## Checklist

1. Review `src/ht/map.c` and `src/ht/table.c` to restate the probing rules, resize thresholds, and ownership expectations before writing code.
2. Capture pseudocode for `ttak_insert_to_map`, `ttak_map_get_key`, and `ttak_delete_from_map` in this folder so you can trace how entries move between buckets.
3. Recreate the map helpers in the main source tree, then run `make tests/test_ht` to ensure the regression suite still passes with your implementation.
4. Use the driver (or a custom scratch program) to log how `now` timestamps propagate through the map, and summarize the gotchas in your "What I learned" notes.

# Tutorial 30 – B+ Tree

[`modules/30-bplus-tree`](../modules/30-bplus-tree/README.md) rebuilds the B+ tree found in `src/tree/bplus.c`.

`lesson30_bplus_tree.c` inserts + queries a key; expand it while ensuring splits/merges match the upstream behavior.

## Checklist

1. Write down node capacities, split criteria, and merge rules so you have a spec before touching code.
2. Rebuild init/insert/get/destroy plus the helper routines that balance the tree when nodes overflow or underflow.
3. Augment the lesson driver into a multi-key test harness (or port a favorite B+ test) because no automated test exists yet.
4. Capture diagrams showing how leaves/internal nodes change as you insert sequential and random keys.

# Tutorial 31 – B-Tree

[`modules/31-btree`](../modules/31-btree/README.md) tackles the sibling structure in `src/tree/btree.c`.

`lesson31_btree.c` exercises insert/search; keep extending it to hit deletion paths as you port the code.

## Checklist

1. Compare and contrast the B-tree node rules with the B+ tree lesson so you know what logic must change.
2. Rebuild the insertion, search, and destruction helpers (plus merge/split) according to the lesson.
3. Use custom drivers or scripts in this folder to stress deletion, borrow, and merge cases until you are confident in the implementation.
4. Document invariants (min degree, key ordering) and keep them handy for future maintenance.

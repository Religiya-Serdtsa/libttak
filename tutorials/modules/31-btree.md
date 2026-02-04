# 31 – B-Tree

**Focus:** Clone the general B-Tree implementation used by planners.

**Source material:**
- `src/tree/btree.c`

## Steps
1. Document differences vs B+Tree before coding (value storage, leaf fanout).
1. Copy split/merge/collapse functions.
1. Test deletion heavy workloads so you can compare to upstream performance.
1. Log balancing steps to verify they match the reference output.

## Checks
- Delete-heavy workloads maintain valid search tree.
- Logs show the same rebalance order as upstream.

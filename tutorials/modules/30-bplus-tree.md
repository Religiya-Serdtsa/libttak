# 30 – B+Tree

**Focus:** Rebuild B+Tree split/merge mechanics.

**Source material:**
- `src/tree/bplus.c`

## Steps
1. Recreate node layout and order constants.
1. Clone insert/split path and keep rebalancing identical.
1. Implement range iterators and ensure they reuse buffer pools from Lesson 5.
1. Test with sequential + random inserts to reveal balancing issues.

## Checks
- Tree height stays minimal for ordered inserts.
- Iterators walk pages in ascending order.

# 22 – Memory Tree

**Focus:** Clone the memory tree GC integration and cleanup passes.

**Source material:**
- `src/mem_tree/mem_tree.c`
- `temp_include/ttak_mem_tree.h`

## Steps
1. Rebuild node structs plus parent/child links exactly.
1. Implement traversal helpers used during cleanup, keeping recursion depth manageable.
1. Integrate the epoch GC hooks from Lesson 21.
1. Add tests that simulate allocations across multiple owners.

## Checks
- Cleanup traversals remove orphaned nodes deterministically.
- Tree integrates with owners + GC without leaking.

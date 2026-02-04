# Tutorial 22 – Mem Tree

Just like the lesson in [`modules/22-mem-tree`](../modules/22-mem-tree/README.md), this workspace is for the tracking tree built inside `src/mem_tree/mem_tree.c`.

`lesson22_mem_tree.c` stores and removes tracked nodes; build/run it to visualize how nodes move through the tree.

## Checklist

1. Outline the responsibilities of `ttak_mem_tree_t` (ordering by expiration, safe iteration) and keep the drawing nearby.
2. Rebuild `ttak_mem_tree_init/add/remove/destroy` along with the helper that prunes expired nodes.
3. Because there is no dedicated automated test, lean on the lesson driver or a custom unit to validate insertion/removal orderings.
4. Document how you verified the tree stays balanced and how you avoided double-free scenarios.

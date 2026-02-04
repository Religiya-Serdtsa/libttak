# Tutorial 29 – AST Walking

[`modules/29-ast-walking`](../modules/29-ast-walking/README.md) focuses on the visitor utilities in `src/tree/ast.c`.

`lesson29_ast_walking.c` builds a miniature tree; rebuild it to debug allocations and traversal order.

## Checklist

1. Diagram the AST node structure (children list, parent pointers, timestamps) and keep traversal pseudocode here.
2. Re-implement node creation, child insertion, and destroy helpers while mirroring the ownership rules documented in the lesson.
3. Because there is no dedicated automated test, grow the driver into a mini traversal harness that asserts preorder/postorder sequences.
4. Write down how you validated destructor correctness so later tree lessons inherit the safeguards.

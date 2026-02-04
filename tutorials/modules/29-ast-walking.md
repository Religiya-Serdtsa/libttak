# 29 – AST Walking

**Focus:** Clone the AST builder + visitor utilities.

**Source material:**
- `src/tree/ast.c`

## Steps
1. Map grammar -> node struct fields before coding.
1. Clone parser and visitor functions while keeping recursion depth manageable.
1. Add tests that parse small expressions and ensure evaluation matches upstream.
1. Record how visitors interact with memory ownership.

## Checks
- AST builder handles invalid syntax like reference.
- Visitor results identical for sample expression suite.

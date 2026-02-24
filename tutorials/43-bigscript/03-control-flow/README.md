# 03 - Control Flow

BigScript supports conditional execution using `if` statements and standard comparison operators.

## Comparison Operators

The following comparison operators are available for integers:
- `==` : Equal to
- `!=` : Not equal to
- `<`  : Less than
- `>`  : Greater than
- `<=` : Less than or equal to
- `>=` : Greater than or equal to

## If Statements

The `if` statement evaluates a condition. If the condition is true (non-zero), the block is executed.

```rust
if (seed == 100) {
  return 1;
}
```

*Note: Currently, BigScript only supports the `if` statement. `else` and `else if` are not yet implemented in the core engine.*

## Running the Example

```bash
gcc main.c -o control_flow -lttak
./control_flow
```

Expected output:
```
Seed 100 -> Result: 1
Seed 25 -> Result: 2
Seed 75 -> Result: 0
```

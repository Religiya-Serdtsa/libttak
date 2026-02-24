# 04 - Built-in Functions

BigScript provides several built-in functions for common mathematical operations.

## `s(n)`

The most important built-in function is `s(n)`, which calculates the **sum of proper divisors** of an integer `n`.

```rust
if (s(seed) == seed) {
  return 1; // Found a perfect number!
}
```

## Input Arguments: `seed` and `sn`

When `ttak_bigscript_eval_seed` is called from C, it passes two values to the `main` function:
1.  `seed`: The number being evaluated.
2.  `sn`: The precomputed `s(seed)` value.

Using `sn` is faster because the C host has often already calculated this value before invoking the script. However, you can also call `s(n)` explicitly within the script for any number.

## Running the Example

```bash
gcc main.c -o builtins -lttak
./builtins
```

Expected output:
```
Seed 6 (sn=6) -> Result: 1
Seed 10 (sn=8) -> Result: 0
```

# 05 - Real and Complex Numbers

BigScript isn't limited to integers. It also supports `BigReal` and `BigComplex` types.

## Type Conversion and Construction

-   `real(n)`: Converts an integer `n` to a real number.
-   `complex(re, im)`: Constructs a complex number from real/integer components.

## Example

```rust
fn main(seed, sn) {
  let r = real(seed);
  let c = complex(r, 10);
  return c;
}
```

In this example, if `seed` is 5, the script returns the complex number `5.0 + 10.0i`.

## Handling Results in C

When a script returns a non-integer value, the `is_found` flag in `ttak_bigscript_value_t` is set to `true`, and you can inspect the `value.type` and `value.v` members to extract the result.

## Running the Example

```bash
gcc main.c -o complex_math -lttak
./complex_math
```

Expected output:
```
Result: 5 + 10i
```

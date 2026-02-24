# 01 - Getting Started with BigScript

In this lesson, you'll learn the basic structure of a BigScript program and how to execute it using the `libttak` C API.

## The BigScript Program

A BigScript program consists of functions. The entry point is always a function named `main` that takes two arguments: `seed` and `sn`.

```rust
fn main(seed, sn) {
  return 42;
}
```

-   `seed`: A high-precision integer passed to the script.
-   `sn`: The precomputed "sum of proper divisors" of the seed.
-   `return`: Scripts return a value. In BigScript, a non-zero integer return value often signifies "found" or "true".

## Running the Example

1.  Compile the runner:
    ```bash
    gcc main.c -o getting_started -lttak
    ```
2.  Run the executable:
    ```bash
    ./getting_started
    ```

You should see:
```
Compiling script...
Evaluating script with seed=10...
Result: 42
```

## What happened?
-   The C code calls `ttak_bigscript_compile` to turn the source string into a program object.
-   It creates a `ttak_bigscript_vm_t` which provides the execution environment.
-   `ttak_bigscript_eval_seed` runs the `main` function with the provided `seed` and `sn`.

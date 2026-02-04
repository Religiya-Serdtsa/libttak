# Tutorial 11 – Portable Atomics

[`modules/11-portable-atomics`](../modules/11-portable-atomics/README.md) walks through the `stdatomic` fallbacks in `src/atomic/atomic.c` and `include/stdatomic.h`.

`lesson11_portable_atomics.c` gives you a quick sanity check for the 64-bit helpers; rebuild it whenever you adjust intrinsics or memory-ordering macros.

## Checklist

1. Map out which atomic primitives rely on compiler builtins versus hand-rolled mutexes so you know what needs cloning.
2. Port `ttak_atomic_*` functions and keep notes about any platforms that require special casing in `include/stdatomic.h`/`temp_include/stdatomic.h`.
3. Run `make tests/test_atomic` to cover the regression set plus any additional experiments you encode in this folder.
4. Document the memory-ordering guarantees you preserved so later concurrency modules can rely on them.

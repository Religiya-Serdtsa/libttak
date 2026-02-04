# Tutorial 27 – NTT Module

[`modules/27-ntt-module`](../modules/27-ntt-module/README.md) dives into the Number-Theoretic Transform implementation found in `src/math/ntt.c`.

`lesson27_ntt_module.c` performs a forward + inverse transform; build it after every major change to confirm round-trips still work.

## Checklist

1. Capture the modulus/primitive-root table and how twiddle factors are cached so you replicate the structure accurately.
2. Rebuild `ttak_ntt_transform`, pointwise multiply, and CRT combine helpers as described in the lesson.
3. Run `make tests/test_math_advanced` since it already performs round-trips and pointwise-multiply checks.
4. Store any additional test vectors or plots here for future debugging.

# Tutorial 25 – Sum of Divisors

[`modules/25-sum-divisors`](../modules/25-sum-divisors/README.md) focuses on the arithmetic helpers in `src/math/sum_divisors.c` that power the aliquot app.

`lesson25_sum_divisors.c` queries `ttak_sum_proper_divisors_u64`; compile it as you verify sigma calculations.

## Checklist

1. Note the formulas for factoring `n` and combining prime powers into divisor sums, then keep worked examples in this folder.
2. Clone the public helpers (u64 path plus arbitrary-precision variants if the lesson requires them) and ensure overflow detection matches the reference implementation.
3. Validate against known amicable pairs by extending the driver or writing a quick script inside this workspace.
4. Record the performance characteristics you observe so you know when to switch to the NTT/FFT acceleration in later lessons.

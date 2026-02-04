# Tutorial 24 – Bigmul + Bigreal

[`modules/24-bigmul-bigreal`](../modules/24-bigmul-bigreal/README.md) extends the bigint work into convolution helpers and arbitrary-precision real numbers.

`lesson24_bigmul_bigreal.c` exercises both the `ttak_bigmul_t` workspace and `ttak_bigreal_t`; compile it as you iterate.

## Checklist

1. List the lifetimes for `ttak_bigmul_t` (embedded bigints, scratch buffers) plus the exponent/mantissa rules for bigreals.
2. Rebuild bigmul init/free and the high-level multiply helpers alongside the bigreal add/sub routines.
3. Use both `make tests/test_math` and `make tests/test_math_advanced` since those suites hit the new helpers.
4. Document rounding and normalization decisions so the later FFT/NTT modules stay consistent.

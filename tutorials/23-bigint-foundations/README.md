# Tutorial 23 – Bigint Foundations

[`modules/23-bigint-foundations`](../modules/23-bigint-foundations/README.md) is the first math-heavy lesson and focuses on `src/math/bigint.c`.

`lesson23_bigint_foundations.c` drives the init/add/hex helpers; rebuild it after each milestone to confirm your bigint still serializes correctly.

## Checklist

1. Capture the struct layout (SSO buffer vs. dynamic limbs) and the invariants for `ttak_bigint_t`.
2. Rebuild init/free/set/add/serialization helpers exactly as documented so the advanced math lessons have a stable base.
3. Run `make tests/test_math` frequently—those tests hit init, mersenne mod, unit ops, and multiplication scaffolding.
4. Archive any tricky carry-handling notes so you can revisit them when debugging later math modules.

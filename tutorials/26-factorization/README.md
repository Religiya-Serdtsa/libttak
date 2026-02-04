# Tutorial 26 – Factorization

[`modules/26-factorization`](../modules/26-factorization/README.md) rebuilds the factoring helpers under `src/math/factor.c`.

`lesson26_factorization.c` factors 360 as a sanity check; reuse it (and extend it) to validate your routines.

## Checklist

1. Write down the algorithm mix (trial division, wheel, rho, etc.) that the lesson expects and capture any randomness requirements.
2. Implement `ttak_factor_u64` plus its supporting structures, making sure allocations and timestamps line up with the memory subsystem.
3. Run the sample program with various inputs and augment it with asserts or reference comparisons to external calculators.
4. Document failure cases (prime overflow, allocation errors) to revisit if the aliquot app exposes them.

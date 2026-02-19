# Tutorial 26 – Factorization

[`modules/26-factorization`](../modules/26-factorization/README.md) rebuilds the factoring helpers under `src/math/factor.c`.

`lesson26_factorization.c` factors 360 as a sanity check; reuse it (and extend it) to validate your routines.

## Engine Notes

The production engine now mirrors the EPR design goals:

1. **Trial Division Wheel** – Quickly strips the first 100 primes (including 2) while respecting the library allocator contracts.
2. **Deterministic Miller-Rabin (64-bit)** – Bases {2,3,5,7,11,13} avoid undefined behavior and allow us to skip ECM for most workloads.
3. **Pollard-Rho with Brent Switching** – Pure C, lock-free randomization seeded from the caller’s timestamp. It gracefully degrades into deterministic trial division if a stubborn composite survives 32 attempts.
4. **Bigint Fast Path** – When a bigint fits inside 64 bits we reuse the optimized `ttak_factor_u64()` output to avoid repeated allocations.

Document any regressions or late-stage fallbacks when you extend this tutorial; the aliquot tracker depends on deterministic outputs when the CPU backend runs without GPU help.

## Checklist

1. Write down the algorithm mix (trial division, wheel, rho, etc.) that the lesson expects and capture any randomness requirements.
2. Implement `ttak_factor_u64` plus its supporting structures, making sure allocations and timestamps line up with the memory subsystem.
3. Run the sample program with various inputs and augment it with asserts or reference comparisons to external calculators.
4. Document failure cases (prime overflow, allocation errors) to revisit if the aliquot app exposes them.

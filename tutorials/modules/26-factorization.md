# 26 – Factorization Helpers

**Focus:** Clone divisor/factor utilities referenced by aliquot tracker.

**Source material:**
- `src/math/factor.c`

## Steps
1. Rebuild Pollard Rho / trial division helpers as implemented upstream.
1. Respect randomness seeding behaviors (document them).
1. Make the factoring pipeline composable.
1. Benchmark vs upstream to ensure runtime is similar.

## Checks
- Factoring results identical for 64-bit numbers.
- Benchmark parity within 5% of reference on dev box.

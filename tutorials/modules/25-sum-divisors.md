# 25 – Sum of Divisors

**Focus:** Clone the divisor-sum helpers including safe-mode policy.

**Source material:**
- `src/math/sum_divisors.c`

## Steps
1. Replicate the prime sieve or caching approach used by the reference.
1. Implement the safe-mode fallback exactly—log when you switch plans.
1. Add instrumentation to track how often the fast path is used.
1. Compare outputs with known aliquot sequence values.

## Checks
- Safe mode engages when numbers exceed the configured bound.
- Results match published aliquot tables for tested values.

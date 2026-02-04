# 27 – NTT Module

**Focus:** Clone the Number Theoretic Transform implementation.

**Source material:**
- `src/math/ntt.c`

## Steps
1. Copy modulus/primes tables exactly.
1. Rebuild forward/inverse transforms and twiddle factor caching.
1. Ensure modular inverse math uses the same overflow guards.
1. Cross-check results with a Python NTT script.

## Checks
- Forward+inverse transform round-trip random sequences.
- Performance within 10% of reference for 4096-size transforms.

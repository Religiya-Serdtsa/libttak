# 23 – BigInt Foundations

**Focus:** Clone the base big integer representation plus SSO path.

**Source material:**
- `src/math/bigint.c`
- `include/ttak/bigint.h`

## Steps
1. Recreate limb layout, SSO thresholds, and constructors.
1. Clone addition/subtraction plus normalization.
1. Write tests that round-trip 256-bit values via `ttak_bigint_set_u256`.
1. Document how overflow checks signal errors.

## Checks
- SSO path triggers for <=256-bit numbers.
- Arithmetic agrees with upstream for random values.

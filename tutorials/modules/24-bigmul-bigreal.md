# 24 – BigMul + BigReal

**Focus:** Rebuild arbitrary-precision multiply and real helpers.

**Source material:**
- `src/math/bigmul.c`
- `src/math/bigreal.c`

## Steps
1. Clone Karatsuba/FFT selection logic as implemented in the reference.
1. Implement normalization after multiplication to avoid leading zeros.
1. Recreate big real conversions that bridge to decimal strings.
1. Test by multiplying random pairs and comparing to Python's big ints.

## Checks
- Mul selects algorithms deterministically based on operand size.
- Big real conversions round-trip sample inputs.

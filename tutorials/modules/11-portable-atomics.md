# 11 – Portable Atomics

**Focus:** Duplicate the fallback `stdatomic` shim used on compilers without full support.

**Source material:**
- `src/atomic/atomic.c`
- `include/stdatomic.h`

## Steps
1. Review the feature-detection macros and copy them verbatim.
1. Clone each `_Atomic` wrapper and ensure the memory order enums line up with the C11 spec.
1. Reproduce the compile-time asserts the reference uses to keep struct sizes stable.
1. Build with TinyCC (or another limited compiler) to prove the fallback works.

## Checks
- `__TTAK_NEEDS_PORTABLE_STDATOMIC__` toggles exactly like the original.
- Fallback compile succeeds on the target compiler.

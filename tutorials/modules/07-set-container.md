# 07 – Set Container

**Focus:** Rebuild the ordered set wrapper that uses the hash table underneath.

**Source material:**
- `src/container/set.c`
- `tests/container/test_set.c`

## Steps
1. Map every public API call to either the hash table or iterator utilities so you recognize code reuse.
1. Clone iteration helpers and keep ordering semantics identical to the reference.
1. Add asserts for duplicate insert attempts.
1. Write down how the set exposes snapshots so later lessons can reference the same contract.

## Checks
- Set API mirrors docs in `include/ttak/set.h`.
- Duplicate insert detection fires in your local tests.

# 20 – Owner Subsystem

**Focus:** Clone the owner/guard API so resources track lifetimes correctly.

**Source material:**
- `src/mem/owner.c`
- `include/ttak/owner.h`

## Steps
1. Recreate structs + initialization (RW locks, reference counts).
1. Implement register/unregister functions while keeping lock ordering identical to upstream.
1. Copy the observer callbacks triggered on release.
1. Unit-test by registering fake handles and ensuring release order matches the reference log.

## Checks
- Owner state machine transitions match docs.
- Locks do not deadlock when nested as described in the manual.

# 35 – Context Bridge

**Focus:** Clone the unsafe context bridge and shared-memory ownership rules.

**Source material:**
- `src/unsafe/context.c`
- `include/ttak/unsafe/context.h`
- `temp_include`

## Steps
1. Recreate the shared-memory mapping logic exactly (platform conditionals and fallbacks).
1. Implement ownership inheritance, ensuring locks are held whenever buffers are touched.
1. Copy the cleanup routines that detach shared memory safely.
1. Write a smoke test that launches two processes and passes contexts around.

## Checks
- Shared memory segments clean up on crash or normal exit.
- Ownership traces prove there is a single writer at any time.

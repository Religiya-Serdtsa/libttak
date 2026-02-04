# 21 – Epoch GC

**Focus:** Recreate the epoch-based GC used by shared structures.

**Source material:**
- `src/mem/epoch_gc.c`

## Steps
1. Map out the epoch rotation flow on paper before coding.
1. Clone the thread-local guard macros and watchers.
1. Implement reclamation so retired nodes free only after all readers exit the epoch.
1. Simulate heavy read traffic to ensure GC liveness.

## Checks
- Epoch rotation never frees memory still in use.
- Stress tests show bounded backlog of retired nodes.

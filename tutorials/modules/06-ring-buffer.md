# 06 – Ring Buffer

**Focus:** Clone the bounded ring buffer implementation that powers queues and timers.

**Source material:**
- `src/container/ringbuf.c`
- `tests/container/test_ringbuf.c`

## Steps
1. Implement push/pop with wrap-around arithmetic first; add error handling once indexing works.
1. Cover the blocking and non-blocking paths if the reference exposes both.
1. Write debug prints for head/tail while testing so you can reason about contention later.
1. Verify `ttak_ringbuf_compact` keeps ordering intact.

## Checks
- Push/pop works for lengths up to capacity and rejects `capacity+1` pushes.
- Ring buffer invariants hold after random enqueue/dequeue sequences.

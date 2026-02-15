# Tutorial 06 – Ring Buffer

Use this folder for [`modules/06-ring-buffer`](../modules/06-ring-buffer/README.md) as you re-create the fixed-capacity queue that lives in `src/container/ringbuf.c`.

`lesson06_ring_buffer.c` performs push/pop/count exercises; compile it with `make` to watch head/tail transitions while you debug.

## Checklist

1. List the invariants that must hold for `ttak_ringbuf_t` (empty/full rules, wrap-around math, timestamp usage) and keep the notes here.
2. Rebuild `ttak_ringbuf_create`, `ttak_ringbuf_push`, `ttak_ringbuf_pop`, and the helper predicates so that they match the legacy semantics.
3. Run `make tests/test_ringbuf` to execute the ring-buffer regression plus any extra stress cases you add in this folder (`tests/test_ringbuf.c`).
4. Record which overflow/underflow bugs you hit and how you resolved them so Lesson 07 builds on a clean foundation.
